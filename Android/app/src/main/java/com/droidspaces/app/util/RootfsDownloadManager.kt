package com.droidspaces.app.util

import android.app.DownloadManager
import android.content.Context
import android.net.Uri
import android.os.Environment
import com.droidspaces.app.R
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOn
import java.io.File

sealed class DownloadStatus {
    data class Progress(val percent: Int) : DownloadStatus()
    data class Completed(val fileUri: Uri) : DownloadStatus()
    data class Failed(val reason: String) : DownloadStatus()
}

object RootfsDownloadManager {

    private const val POLL_MS = 500L

    /**
     * Starts a download and emits [DownloadStatus] updates as a Flow.
     * Completes with [DownloadStatus.Completed] (content:// URI ready to install)
     * or [DownloadStatus.Failed] on any error.
     */
    fun enqueue(context: Context, asset: RootfsAsset): Long {
        val dm = context.getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager
        val filename = asset.uniqueFilename
        val destFile = File(
            Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
            filename
        )
        destFile.delete()
        val request = DownloadManager.Request(Uri.parse(asset.downloadUrl)).apply {
            setTitle(filename)
            setDescription(context.getString(R.string.repo_notification_description))
            setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE)
            setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, filename)
            setAllowedOverMetered(true)
            setAllowedOverRoaming(true)
        }
        return dm.enqueue(request)
    }

    fun pollFlow(context: Context, asset: RootfsAsset, downloadId: Long): Flow<DownloadStatus> = flow {
        val dm = context.getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager
        val filename = asset.uniqueFilename
        val destFile = File(
            Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
            filename
        )
        val query = DownloadManager.Query().setFilterById(downloadId)
        var done = false

        while (!done) {
            delay(POLL_MS)
            val cursor = dm.query(query)
            if (cursor == null || !cursor.moveToFirst()) {
                emit(DownloadStatus.Failed(context.getString(R.string.repo_dl_error_unknown)))
                return@flow
            }

            val status   = cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS))
            val received = cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR))
            val total    = cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_TOTAL_SIZE_BYTES))
            cursor.close()

            when (status) {
                DownloadManager.STATUS_RUNNING,
                DownloadManager.STATUS_PENDING,
                DownloadManager.STATUS_PAUSED -> {
                    val pct = if (total > 0) ((received * 100) / total).toInt() else 0
                    emit(DownloadStatus.Progress(pct))
                }

                DownloadManager.STATUS_SUCCESSFUL -> {
                    done = true
                    val contentUri = dm.getUriForDownloadedFile(downloadId)
                        ?: Uri.fromFile(destFile)
                    emit(DownloadStatus.Completed(contentUri))
                }

                DownloadManager.STATUS_FAILED -> {
                    done = true
                    emit(DownloadStatus.Failed(getFailureReason(context, dm, downloadId)))
                }
            }
        }
    }.flowOn(Dispatchers.IO)

    fun cancel(context: Context, downloadId: Long) {
        val dm = context.getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager
        dm.remove(downloadId)
    }

    private fun getFailureReason(context: Context, dm: DownloadManager, id: Long): String {
        val cursor = dm.query(DownloadManager.Query().setFilterById(id))
            ?: return context.getString(R.string.repo_dl_error_unknown)
        return if (cursor.moveToFirst()) {
            val code = cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_REASON))
            cursor.close()
            when (code) {
                DownloadManager.ERROR_CANNOT_RESUME       -> context.getString(R.string.repo_dl_error_cannot_resume)
                DownloadManager.ERROR_DEVICE_NOT_FOUND    -> context.getString(R.string.repo_dl_error_device_not_found)
                DownloadManager.ERROR_FILE_ALREADY_EXISTS -> context.getString(R.string.repo_dl_error_file_exists)
                DownloadManager.ERROR_FILE_ERROR          -> context.getString(R.string.repo_dl_error_file_error)
                DownloadManager.ERROR_HTTP_DATA_ERROR     -> context.getString(R.string.repo_dl_error_http_data)
                DownloadManager.ERROR_INSUFFICIENT_SPACE  -> context.getString(R.string.repo_dl_error_no_space)
                DownloadManager.ERROR_TOO_MANY_REDIRECTS  -> context.getString(R.string.repo_dl_error_too_many_redirects)
                DownloadManager.ERROR_UNHANDLED_HTTP_CODE -> context.getString(R.string.repo_dl_error_unhandled_http)
                DownloadManager.ERROR_UNKNOWN             -> context.getString(R.string.repo_dl_error_unknown)
                404                                       -> context.getString(R.string.repo_dl_error_not_found)
                else                                      -> context.getString(R.string.repo_dl_error_code, code)
            }
        } else {
            cursor.close()
            context.getString(R.string.repo_dl_error_unknown)
        }
    }
}

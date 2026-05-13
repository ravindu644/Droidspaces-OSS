package com.droidspaces.app.util

import android.content.Context
import org.json.JSONArray

object ContributorManager {
    fun load(context: Context): List<Contributor> = try {
        val json = context.assets.open("contributors.json").bufferedReader().readText()
        val arr = JSONArray(json)
        List(arr.length()) { i ->
            val o = arr.getJSONObject(i)
            Contributor(
                login = o.getString("login"),
                commits = o.getInt("commits"),
                githubUrl = o.getString("github_url"),
                avatarB64 = o.optString("avatar_b64", "")
            )
        }
    } catch (e: Exception) {
        emptyList()
    }
}

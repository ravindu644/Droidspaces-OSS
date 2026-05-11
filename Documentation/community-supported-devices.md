# Community-supported Android devices

> [!NOTE]
>
> If you want to add your own Droidspaces kernel to this table, check out the [Contribution guidelines](#contribution-guidelines) section.

This document is a community-maintained compatibility list for Android devices known to run Droidspaces successfully. It is intended to help people choose phones for self-hosting, especially when buying second-hand hardware.


| Device Name | Model Number | Android / ROM | Baseband / Build | Kernel version | Root Method | Droidspaces Mode | GPU Acceleration | Status | Maintainer | Kernel Source | Download Link | Additional notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Galaxy A16 5G** | SM-A166P | One UI 6.1 - Android 14 | `A166PXXS4AYC1` | `5.15.148` | KernelSU-Next v3.1.0 | Both | Virgl only | Working | [@ravindu644](https://github.com/ravindu644) | [Source](https://github.com/ravindu644/android_kernel_a166p) | [Download](https://github.com/ravindu644/android_kernel_a166p/releases/download/r20260402-c17abb78/KernelSU-SM-A166P-OneUI6-A166PXXS4AYC1-r20260402-c17abb78.tar.zip) | - |
| **Galaxy A16 5G** | SM-A166P | One UI 7.0 - Android 15 | `A166PXXU4BYE6` | `5.15.167` | KernelSU-Next v1.1.1 | Both | Virgl only | Working | [@ravindu644](https://github.com/ravindu644) | [Source](https://github.com/ravindu644/android_kernel_a166p) | [Download](https://github.com/ravindu644/android_kernel_a166p/releases/download/ubuntu-x-docker/Ubuntu-x-KernelSU-Next-SM-A166P-dev.zip) | - |
| **Galaxy M14 4G** | SM-M145F | One UI 7.0 - Android 15 | `M145FXXS8DYH1` | `5.15.167` | KernelSU-Next v1.1.1 | Both | Turnip & Virgl | Working | [@ravindu644](https://github.com/ravindu644) | [Source](https://github.com/ravindu644/android_kernel_sm_m145f) | [Download](https://github.com/ravindu644/android_kernel_sm_m145f/releases/download/v1/SM-M145F-Droidspaces-KSUNv1.1.1-M145FXXS8DYH1.tar.zip) | - |
| **Galaxy S10 (Japanese variant)** | SCV41 / SM-G973J | One UI 4.1 - Android 12 | `SCV41KDU1DWC1` | `4.14.190` | KernelSU-Next v3.1.0 | Both | Turnip & Virgl | Working | [@ravindu644](https://github.com/ravindu644) | [Source](https://github.com/ravindu644/samsung_kernel_SCV41_droidspaces) | [Download](https://github.com/ravindu644/samsung_kernel_SCV41_droidspaces/releases/download/v2/Droidspaces-KSUN-Samsung-SCV41.tar) | - |
| **Poco X2** | M1912G7Bx | BlissROM-v16.5 - Android 13 | - | `4.14.274` | KernelSU-Next v1.1.1 | Both | Turnip & Virgl | Working | [@ravindu644](https://github.com/ravindu644) | [Source](https://github.com/ravindu644/phoenix-blissROM-A13-droidspaces) | [Download](https://github.com/ravindu644/phoenix-blissROM-A13-droidspaces/releases/download/v1/Droidspaces-KSUNv1.1.1-phoenix.zip) | - |

## Contribution guidelines

To keep this list useful and reliable, please follow these rules when adding or updating entries:

- Add one device per row.
- Fill every column completely.
- Contributions should be honest, complete, and verifiable.
- Provide a direct downloadable kernel archive (`zip`, `tar`, `img`) or a downloadable kernel package; inexperienced users should not need to compile the kernel themselves.
- Provide the exact source code link for your Droidspaces kernel in the Kernel Source column.
- Document the exact `Baseband / Build` string.
- Specify the `Root Method` used, such as `Magisk`, `KernelSU`, or `APatch`.
- Set `Status` to one of: `Working`, `Partial`, or `Unusable`.
- In Notes, include known quirks and issues, additional setup steps, and recommended workloads.
- Submit contributions through GitHub pull requests.
- Update entries when status changes, and keep the information current.

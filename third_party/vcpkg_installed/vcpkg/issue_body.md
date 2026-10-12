Package: libsamplerate:x64-windows@0.2.2#1

**Host Environment**

- Host: x64-windows
- Compiler: MSVC 19.42.34435.0
- CMake Version: 4.3.2
-    vcpkg-tool version: 2026-04-08-e0612b42ce44e55a0e630f2ee9d3c533a63d8bc1
    vcpkg-scripts version: aa40adda 2026-05-23 (10 days ago)

**To Reproduce**

`vcpkg install `

**Failure logs**

```
Downloading https://github.com/libsndfile/libsamplerate/archive/0.2.2.tar.gz -> libsndfile-libsamplerate-0.2.2.tar.gz
error: curl operation failed with error code 7 (Could not connect to server).
error: Not a transient network error, won't retry download from https://github.com/libsndfile/libsamplerate/archive/0.2.2.tar.gz
note: If you are using a proxy, please ensure your proxy settings are correct.
Possible causes are:
1. You are actually using an HTTP proxy, but setting HTTPS_PROXY variable to `https://address:port`.
This is not correct, because `https://` prefix claims the proxy is an HTTPS proxy, while your proxy (v2ray, shadowsocksr, etc...) is an HTTP proxy.
Try setting `http://address:port` to both HTTP_PROXY and HTTPS_PROXY instead.
2. If you are using Windows, vcpkg will automatically use your Windows IE Proxy Settings set by your proxy software. See: https://github.com/microsoft/vcpkg-tool/pull/77
The value set by your proxy might be wrong, or have same `https://` prefix issue.
3. Your proxy's remote server is out of service.
If you believe this is not a temporary download server failure and vcpkg needs to be changed to download this file from a different location, please submit an issue to https://github.com/Microsoft/vcpkg/issues
CMake Error at scripts/cmake/vcpkg_download_distfile.cmake:136 (message):
  Download failed, halting portfile.
Call Stack (most recent call first):
  scripts/cmake/vcpkg_from_github.cmake:120 (vcpkg_download_distfile)
  ports/libsamplerate/portfile.cmake:1 (vcpkg_from_github)
  scripts/ports.cmake:206 (include)



```

**Additional context**

<details><summary>vcpkg.json</summary>

```
{
  "name": "equalizerapo-vst3",
  "version-string": "1.4.2",
  "dependencies": [
    {
      "name": "fftw3",
      "features": [
        "threads",
        "avx2"
      ]
    },
    "libsndfile",
    "libsamplerate"
  ]
}

```
</details>

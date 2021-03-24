# grate
Grate is an anagram of "tegra", actually it is a collection of open source reverse-engineering tools aiming at the NVIDIA Tegra2+ 3D engine.

## Documentation

We have several useful documents of the Tegra architecture on our [wiki](https://github.com/grate-driver/grate/wiki). In particular:
* [Command Stream](https://github.com/grate-driver/grate/wiki/Command-stream)
* [MMIO Registers](https://github.com/grate-driver/grate/wiki/MMIO-Registers)
* [Vertex Shader ISA](https://github.com/grate-driver/grate/wiki/Vertex-Shader-ISA)
* [Fragment Shader ISA](https://github.com/grate-driver/grate/wiki/Fragment-Shader-ISA)
* [Shader Linking](https://github.com/grate-driver/grate/wiki/Shader-Linking)
* [Geometry Submission](https://github.com/grate-driver/grate/wiki/Geometry-Submission)


xavier交叉编译不过，一下检查失败:
PKG_CHECK_MODULES(DevIL, ILU)
PKG_CHECK_MODULES(PNG, libpng) 
PKG_CHECK_MODULES(DRM, libdrm)

对于DevIL：在xavier中找不到libILU库和相关头文件

对于PNG：在xavier中能找到libpng库和相关头文件，只需要仿照/usr/lib/x86_64-linux-gnu/libpng.pc添加/usr/lib/aarch64-linux-gnu/libpng.pc,或者指定环境变量PNG_CFLAGS(libpng头文件路径)和PNG_LIBS(libpng库路径)

对于DRM：在xavier中能找到libdrm库和相关头文件，只需要仿照/usr/lib/x86_64-linux-gnu/libdrm.pc添加/usr/lib/aarch64-linux-gnu/libdrm.pc,或者指定环境变量DRM_CFLAGS(libdrm头文件路径)和DRM_LIBS(libdrm库路径)
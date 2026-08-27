# Vendor 芯片库层

本目录只存放目标固件实际编译需要的 CMSIS、器件头文件、启动文件和 DDL 源码。
应用层不得包含本目录中的任何头文件；只有 `driver/` 与 `project/keil/` 可以依赖它。

对应许可文本保存在 `vendor/LICENSES/`。厂商完整 SDK、示例工程和文档不放入本仓库。

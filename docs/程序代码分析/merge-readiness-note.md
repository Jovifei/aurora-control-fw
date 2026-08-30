# 合并就绪审阅说明

本分支用于交付 300W 工程函数级代码分析、APP/Driver 注释规范化、UTF-8/乱码审计与完整交付报告。

合并前必须确认：

- 固件 C/H 编译器可见 Token 与基线一致；
- APP/Driver 文件头和函数头符合项目注释规范；
- 所有文本严格 UTF-8、无 BOM、无常见乱码；
- `tools/run_checks.py`、注释检查和编码检查全部通过；
- 程序代码分析 Markdown 相对链接和 SVG XML 有效；
- 不解除任何模拟、COMP、Keil、低压台架或功率总门禁。

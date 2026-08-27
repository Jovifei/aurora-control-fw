# 09 · 内部Flash参数保存

没有外部EEPROM。最后两个512字节Flash页作为A/B双页日志：

```text
Magic + Version + Length + Sequence + CRC + Commit Marker + Payload
```

先擦备用页，写未提交Header与Payload，最后单独写Commit Marker。任意中途掉电都保留旧页。启动时选择CRC有效且Sequence更新的一页。

Flash地址为 `0xFC00` 与 `0xFE00`；Scatter把应用代码限制在 `0x0000～0xFBFF`。页大小和擦写行为仍须在最终数据手册、Keil map和实片上复核。

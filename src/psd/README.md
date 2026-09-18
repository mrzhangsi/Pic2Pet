# psd/ — PSD 解析与装配（M2）

- `PsdReader` — 自研只读解析器。支持 8bit、RGB/灰度/RGBA、RLE 压缩；
  明确不支持 16/32bit、CMYK/LAB、图层组、智能对象（给出友好报错）
- `Rigger` — 移植 `参考项目/Petra/src/vendor/anime2dr/rigger.js`（MIT）
  - 图层名归一化 + SLOTS 槽位表（depth/group/phys/fade/split）
  - 连通域去噪（< 40px 剔除 + 膨胀 3）、左右眼分离、发丝 strand 峰值检测
  - 锚点：face / eyeL / eyeR / mouth / neckPivot / bodyPivot / faceScale = 脸宽 / 333
  - 缺失 eye_close / mouth_close 时用内置通用部件合成（genericparts 导出为 PNG 资源）
- `PetBin` — `.petbin` 缓存读写，装配一次后落盘，后续启动绕开 PSD 解析

**一致性保障**：同一份 PSD 分别跑 JS 版与 C++ 版，导出锚点/部件 bbox/strand 为 JSON
做逐字段比对，做成自动化测试（`tools/`）。数值差一点手感就变了。

# rig/ — 2.5D 驱动与变形（M2）

- `RigParams` — 36 通道参数 POD（angleX/Y/Z、eyeOpenL/R、mouthOpen、brow、bangL/C/R、physAmp……）
- `Deformer` — 移植 `PsdRuntime.deform()`：眼/嘴/眉局部变形 → 头部绕颈枢旋转（按 depth 加权）
  → 身体摆动 → 呼吸 → 发丝位移
- `SpringSystem` — 发丝 stiff/soft 双弹簧（k=70/16, c=9/1.3）+ 胸弹（k=140, c=4.2）
- `ExpressionTable` — 10 个预设表情，移植自 `Rigged2DView.ts` 的 EXPRESSIONS
- `ActionTable` — 14 个动作关键帧 + 采样器，移植自 `actions.ts`
- `PetDriver` — 聚合行为 / 表情 / 动作 / 交互四路输入，每帧输出 RigParams

顶点量级：约 30 层 × 平均 150 顶点 ≈ 4500 顶点/帧，C++ 下变形 < 0.5ms。

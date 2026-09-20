# rig/ — 2.5D 驱动与变形（M2，**待重新评估**）

> ⚠️ **2026-09-20：本模块状态从"规划中"退回"待重新评估"。**
> 整条链路建立在"用户提供**分层 PSD**"这个前提上，与项目的设计红线
> （素材门槛守在"一张图 + AI 能完成的处理"之内，不做需要人工精修素材的功能）直接冲突。
> 详见 `技术方案.md` 第 15 节。**在有明确答案之前不要动手实现。**

- `RigParams` — 36 通道参数 POD（angleX/Y/Z、eyeOpenL/R、mouthOpen、brow、bangL/C/R、physAmp……）
- `Deformer` — 移植 `PsdRuntime.deform()`：眼/嘴/眉局部变形 → 头部绕颈枢旋转（按 depth 加权）
  → 身体摆动 → 呼吸 → 发丝位移
- `SpringSystem` — 发丝 stiff/soft 双弹簧（k=70/16, c=9/1.3）+ 胸弹（k=140, c=4.2）
- `ExpressionTable` — 10 个预设表情，移植自 `Rigged2DView.ts` 的 EXPRESSIONS
- `ActionTable` — 14 个动作关键帧 + 采样器，移植自 `actions.ts`
- `PetDriver` — 聚合行为 / 表情 / 动作 / 交互四路输入，每帧输出 RigParams

顶点量级：约 30 层 × 平均 150 顶点 ≈ 4500 顶点/帧，C++ 下变形 < 0.5ms。

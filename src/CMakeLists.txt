# 平台抽象 / 渲染 / 应用壳：M0 spike 全部实现
add_subdirectory(platform)
add_subdirectory(render)
add_subdirectory(app)

# M1：素材解码（GIF / APNG / 静态图）
add_subdirectory(media)

# M3：输入监听（全局键盘 / "输入模式"）。注意这是全项目唯一的挂钩式能力，见 src/input/README.md
add_subdirectory(input)

# M3：设计系统与可复用控件（浅色苹果风）。只依赖 Qt，不认识业务；设置窗口在 app/
add_subdirectory(ui)

# AI 教官：学习状态监控（L0 本地规则 + L1 视觉模型 + L2 状态机）。见 teacher/README.md
add_subdirectory(teacher)

# 后续里程碑接入，目录已建好（见各目录 README.md）
# add_subdirectory(psd)        # M2  PSD 只读解析 + rigger 装配 + .petbin
# add_subdirectory(rig)        # M2  RigParams / deform / 弹簧物理 / 表情 / 动作
# add_subdirectory(behavior)   # M3  行为状态机
# add_subdirectory(ui)         # M3  Qt Widgets 设置面板

# 平台抽象 / 渲染 / 应用壳：M0 spike 全部实现
add_subdirectory(platform)
add_subdirectory(render)
add_subdirectory(app)

# M1：素材解码（GIF / APNG / 序列 / 静态图）
add_subdirectory(media)

# 后续里程碑接入，目录已建好（见各目录 README.md）
# add_subdirectory(psd)        # M2  PSD 只读解析 + rigger 装配 + .petbin
# add_subdirectory(rig)        # M2  RigParams / deform / 弹簧物理 / 表情 / 动作
# add_subdirectory(behavior)   # M3  行为状态机
# add_subdirectory(ui)         # M3  Qt Widgets 设置面板

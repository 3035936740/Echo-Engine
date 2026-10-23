# Echo Engine

面向 **Windows 2D 横版动作、平台跳跃与弹幕游戏**的原生引擎与可视编辑器。使用 **C++20、GCC、CMake、Lua 5.4**，作品可导出为独立 exe。

常用制作使用预设、表单和事件指令；Lua 用于高级行为。当前为开发预览版，完整产品范围仍在建设中，参见[需求清单](docs/REQUIREMENTS.md)。

## 快速开始

准备 Windows x64 环境，将 GCC/g++、windres、CMake ≥ 3.22 和 Ninja 加入 PATH。项目仅支持 GCC，不需要 MSVC；第三方源码随仓库提供。

双击根目录 **build.bat** 编译 Release，或运行：

~~~powershell
.\build.ps1
.\build.ps1 -Configuration Debug
.\build.ps1 -RunTests -Jobs 4
~~~

成功后双击：

- `bin/EchoEngine.exe`：打开编辑器。
- `bin/EchoOfKino.exe`：运行示例游戏。

exe 需要旁边的 `project/` 资源目录，不能单独复制作为发布包。发布作品请使用编辑器的公开／加密导出。若 bin 中的程序正被占用，关闭后重新执行构建脚本以同步入口。

也可直接使用 CMake：

~~~powershell
cmake --preset gcc-release
cmake --build --preset gcc-release
ctest --preset gcc-release --output-on-failure
~~~

构建目录为 `out/build/gcc-release/`；Debug 将 preset 换成 `gcc-debug`。生成目录和二进制不纳入版本控制。

## 编辑与试玩

默认浅色主题，可切换石板灰深色主题。地图工具提供图块铺设、独立碰撞、前后景、对象、事件及弹幕部署。

- Ctrl+S 保存；地图画布 Ctrl+Z/Y 撤销／重做。
- F5 试玩，F2 返回编辑，F6 重载脚本。
- 默认 A/D 移动、Space 跳跃、E 互动／对话继续。
- 按住 T 沿历史轨迹回溯；按住 R 快速回溯，玩家沿锁定直线移动，世界恢复历史。
- 键位可在作品设置中修改。编辑器调试 BGM 默认静音，SE 保留。

默认像素风格保留素材原像素，使用整数倍最近邻显示；过滤与 AA 可选。详见[像素显示与静音](docs/PIXEL_OUTPUT.md)。

![编辑器浅色界面](docs/previews/editor-light.png)

## 当前能力

| 制作环节 | 已有基础 |
|---|---|
| 地图与动作 | 图块、前后景／视差、独立碰撞、多地图；单向平台、土狼时间、小跳、梯子、水域、传送带、弹簧、落下／升降平台、可选墙跳／冲刺 |
| 对象与动画 | 模板、实例覆盖、稳定引用、基础敌人／NPC行为、精灵表与逐帧动画／判定框 |
| 事件与数据 | 地图／公共事件、页条件、分支／循环／等待、对话／头像／条件选项、物品数量与单目标任务；分类搜索、多选复制、折叠及撤销 |
| 弹幕 | 固定容量弹池、模式／发射器／组合波次、轨迹预览、目标／阵营、满池策略 |
| 表现与音频 | 昼夜、天气、光照、萤火虫、可选流体／SDF及自定义背景／转场 shader；BGM循环点／渐变与SE |
| 工程 | 多文件保存事务、引用检查、编辑恢复点、公开／认证加密导出 |

这些是已实现的基础能力，不能视为完整 RPG Maker 级产品。完整菜单／玩家存档、多目标任务、通用移动路线、手柄和完整发布检查等仍待建设。弹幕组合场景、GPU与最慢帧尖峰也仍需持续验证。

## 快照与回溯

**所有影响下一帧玩法的状态必须进入 WorldState。** 玩家、敌人、弹幕上一帧位置、动画进度、事件执行、物品／任务、随机数与运行时玩法配置均须完整恢复；Lua 跨帧数据也必须同步进快照。

- 默认历史为 6 秒，可配置 1–30 秒、30／60／120 Hz，也可关闭。
- 历史使用有界预分配环；记录时不扩容，回溯结束截断未来。
- R 与 T 的路径不同；R 默认近弹截停，退出不自动清弹。
- 物理与快照使用浮点逻辑坐标，像素吸附仅发生在渲染层。
- 摄像机与纯视觉 GPU 状态不进入玩法快照，回溯时即时跟随。

当前 GCC x64 完整快照为 **498576 字节**，默认 512 MiB 历史预算可容纳 1076 槽；六秒／60 Hz 含起始帧约 **171.65 MiB**。更长窗口受预算限制，界面显示实际可保留时长。

新增玩法字段后运行 `python tools/generate_snapshot_fields.py` 更新比对描述，并验证「退到最远 → 截断 → 继续」，覆盖 T/R、各记录频率和禁用分支。不要把内存布局直接作为长期磁盘存档格式。

## 示例与文档

从编辑器打开示例目录中的 `project.echo`：

- [Echo of Kino](projects/echo_of_kino/project.echo)：默认游戏工程。
- [对象制作练习室](projects/object_lab/README.md)：模板、动画、弹幕与事件。
- [平台动作练习室](projects/platform_lab/README.md)：六类机关与对象乘客。
- [物品与任务练习室](projects/progression_lab/README.md)：领取、拾取和交付任务。

常用说明：[地图工作区](docs/WORKSPACE.md) · [事件制作](docs/EVENTS.md) · [成组编辑](docs/EVENT_EDITING.md) · [物品与任务](docs/INVENTORY_QUESTS.md) · [弹幕制作](docs/BARRAGE.md) · [高级 Lua](docs/SCRIPT_EXTENSIONS.md)

范围与验证：[需求核对](docs/REQUIREMENTS.md) · [功能边界](docs/ENGINE_SCOPE.md) · [验证记录](docs/VALIDATION.md)

最近记录的完整 Release 回归为 **34/34**；Debug 新增专项通过，尚未完成完整 34 组 Debug 回归。测试结果只证明对应场景，不代表全部需求已经完成。

## 目录

~~~text
echo_engine/    C++ 引擎、运行时和原生编辑器
projects/       独立示例工程及其资源
scripts/        Lua 示例
shaders/        着色器示例
tests/          自动化与原生界面验证
tools/          构建辅助、示例生成和快照描述生成器
third_party/    固定版本的第三方源码
docs/           操作、需求、边界和验证文档
out/、bin/      本地生成文件（忽略）
~~~

根目录的 project.echo 及 assets/audio/levels/events 是保留的可运行工程；resource/ 和 library/ 中保留旧工程资料。当前 CMake 主构建以 echo_engine/ 为入口。

第三方组件与许可证见 [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt)。公开／加密导出都需要核对作品自身素材的使用授权；离线认证加密用于封装与篡改检测，不能承诺资源不可逆向。

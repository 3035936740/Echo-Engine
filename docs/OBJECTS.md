# 对象与模板
打开「对象与模板」，从巡逻、追击、炮台、NPC、门、拾取物、装饰、玩家或 Boss 预设新建。模板保存在 objects.echo；名称最多 63 UTF-8 字节，中文每字通常占 3 字节。

点击「在地图上放置」，左键部署或选择，拖动移动，右键删除。每地图最多 64 个部署，世界实例池共 256 槽。模板可以在多张地图复用；模板删除前列出部署地图和动画绑定，先删除实例或更换引用。

右侧的独立速度、血量、伤害分别控制继承。未勾选时新试玩使用模板属性；「恢复模板继承」清除覆盖。「重进地图保留状态」保持拾取物已收集、敌人死亡及普通对象位置；关闭后重进按模板重新生成。其他地图系统的完整持久规则尚未完成。

碰撞分类和掩码为 16 位位集；默认地形 1、玩家 2、敌人 4、拾取物 8。普通对象之间双方掩码均允许才产生实体响应。纯装饰掩码为 0。旧玩家/Boss 与地图事件的全部分类关系仍需统一。

模板保存后 F5 开始新时间线。对象位置、速度、AI、计时、生命、代次引用、墓碑和非活动地图实例都在 WorldState。新增玩法字段应放进 ObjectState 或其可快照组件。

复杂脚本可用：
```lua
local slot, generation = engine.create_object(1, 800, 400)
if slot then
    engine.state[1] = slot
    engine.state[2] = generation
end
-- 跨帧句柄保存在 engine.state，不能藏在未同步的闭包里。
engine.damage_object(engine.state[1], engine.state[2], 1)
engine.destroy_object(engine.state[1], engine.state[2])
```
池满时返回 nil 并增加 rejected；代次耗尽的槽退役，过期句柄不会引用新对象。普通制作流程不需要这些脚本接口。

## 随平台移动与挤压

模板底部的「随平台移动」控制普通对象乘坐；「平台挤压时死亡」控制致命挤压或脱离。巡逻/追击/NPC预设默认乘坐，炮台/门/拾取物/装饰默认固定；可以从表单修改。实例继承模板策略，运行中的单个实例可用高级Lua覆盖。需要碰撞掩码包含地形位1，且对象分类↔Terrain矩阵允许。

普通对象可以落在单向顶部，乘坐旧移动平台、升降/斜向平台和落下平台；传送带可以向两侧推移，弹簧按每个对象的冷却独立弹射。对象独自站在落下平台上也会触发预警。速度为零的角色也能乘坐，不依赖AI巡逻。平台移动同步搬移巡逻/返回起点，使行为范围跟随平台；自身走动和传送带作用不搬移起点。

携带采用连续扫掠，被推向墙/天花板或横向地图边界时按模板策略处理；向下遇地面则留下对象，不判脚下挤压。升降同子步折返也检查中途挤压。同一平台上的实体乘客相互保持间距，不因依次移动而互相挤死。致命挤压绕过伤害无敌，销毁代次引用；地图部署留下墓碑。非致命挤压脱离平台，本步不重新吸附；不向玩家施加Hit。

ObjectState保存standingPlatformId、springCooldown、ridesPlatforms、crushKills、crushed，连同px/py、AI起点、生命和代次完整回溯。px/py保留携带前的位置，不在对象自身移动时覆盖。失去支撑、关闭乘坐/矩阵、回到重初始化地图时验证接触，避免保留远处平台的附着。非活动地图对象冻结；完整机关跨图持久化仍属于W06待建。

模板保存ECHO_OBJECTS_3，兼容v1/v2：原来有重力的对象开启乘坐，没有重力的保持固定，默认致命挤压。没有修改旧地图部署schema；模板策略以新试玩为准。

~~~lua
local slot, generation = engine.create_object(4, 512, 544)
if slot then
    engine.state[1] = slot
    engine.state[2] = generation
    engine.configure_object_movement(slot, generation, {
        rides_platforms=true, crush_kills=false
    })
end
if engine.state[2] >= 1 then
    local p = engine.get_object(engine.state[1], engine.state[2])
    if p then engine.state[3] = p.standing_platform end
end
~~~

get_object接受0–255槽和正代次；过期/销毁返回nil，参数非法报错，查询是可在只读条件中使用的临时副本。返回x/y/px/py/vx/vy/hp/map（1起）/kind/standing_platform/spring_cooldown及grounded/rides_platforms/crush_kills/crushed。configure_object_movement只接受布尔策略，以候选值全量验证再提交，过期句柄报错；关闭乘坐解除附着，事件Lua错误按WorldState全量回滚。玩家/Boss使用专用参数，不通过本接口改策略。复杂持久逻辑保存在engine.state，示例见scripts/object_movement.lua。

当前覆盖平台乘客；任意实体侧推、叠放箱体、动态体统一挤压和复杂凹角仍待建。它不等同于完整R04/W03已经验收。

玩家/Boss选择时，普通乘坐策略控件会禁用，避免产生无效设置。原生验证实际点击两个复选框、保存往返及特殊角色禁用/切回普通角色启用，并检查浅/深主题。新 [平台练习室](../projects/platform_lab/README.md) 包含乘坐升降平台的NPC，原生启动已验证。

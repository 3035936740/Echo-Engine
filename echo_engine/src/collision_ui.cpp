#include "echo/app.hpp"
#include "echo/collision.hpp"
namespace echo {
void App::collisionCommand(int id) {
    auto refresh = [&] {
        for (int a = 0; a < 8; ++a)
            for (int b = 0; b < 8; ++b)
                SendMessageW(controls[CollisionCell + a * 8 + b], BM_SETCHECK,
                             bool(collisionDraft.masks[a] & (1u << b)), 0);
    };
    if (id == CollisionEditor) {
        command(Edit);
        if (collisionEditor && IsWindow(collisionEditor)) {
            SetForegroundWindow(collisionEditor);
            return;
        }
        collisionDraft = initial_->collisions;
        collisionEditor = CreateWindowW(L"EchoEngineWindow", L"碰撞分类与作用矩阵",
                                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                            ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                                        150, 100, 980, 710, window, nullptr, instance, this);
        auto label = [&](const wchar_t* text, int x, int y, int width) {
            control(L"STATIC", text, 0, x, y, width, 26, 0, collisionEditor);
        };
        const wchar_t* names[] = {L"地形",     L"玩家", L"敌人/Boss", L"NPC",
                                  L"普通物体", L"弹幕", L"攻击",      L"事件/区域"};
        label(L"勾选允许现有交互；关闭会过滤该类别对。两侧掩码必须同时允许。", 24, 20, 922);
        label(L"仍遵守固体、阵营、对象掩码和受击区设置；勾选不会自动产生新的交互。", 24, 57, 922);
        for (int a = 0; a < 8; ++a) {
            label(names[a], 24, 143 + a * 43, 114);
            label(names[a], 144 + a * 100, 108, 98);
            for (int b = 0; b < 8; ++b) {
                auto h = control(L"BUTTON", L"", CollisionCell + a * 8 + b, 144 + b * 100, 141 + a * 43,
                                 92, 30, WS_TABSTOP, collisionEditor);
                if (b < a)
                    EnableWindow(h, FALSE);
            }
        }
        label(L"编辑上三角，另一侧自动同步；默认全部允许，旧工程行为保持。", 24, 505, 922);
        label(L"应用会重置试玩历史；保存工程后作为作品默认值，运行时 Lua 修改可回溯。", 24, 544, 922);
        control(L"BUTTON", L"恢复默认", CollisionDefault, 24, 596, 212, 36, WS_TABSTOP, collisionEditor);
        control(L"BUTTON", L"玩家免疫弹幕预设", CollisionSafe, 253, 596, 290, 36, WS_TABSTOP,
                collisionEditor);
        control(L"BUTTON", L"应用碰撞分类", CollisionApply, 564, 596, 380, 36, WS_TABSTOP,
                collisionEditor);
        refresh();
        return;
    }
    if (!collisionEditor)
        return;
    if (id >= CollisionCell && id < CollisionCell + 64) {
        int a = (id - CollisionCell) / 8, b = (id - CollisionCell) % 8;
        setCollisionPair(collisionDraft, CollisionClass(a), CollisionClass(b),
                         SendMessageW(controls[id], BM_GETCHECK, 0, 0) == BST_CHECKED);
        refresh();
    }
    if (id == CollisionDefault || id == CollisionSafe) {
        collisionDraft = {};
        if (id == CollisionSafe)
            setCollisionPair(collisionDraft, CollisionClass::Player, CollisionClass::Projectile, false);
        refresh();
    }
    if (id == CollisionApply) {
        config.collisions = initial_->collisions = collisionDraft;
        simulation->reset(*initial_, HistoryClearReason::CollisionSettings);
        editor.dirty = true;
        message = "Collision pairs applied; play history restarted";
        refreshChrome();
    }
}
} // namespace echo

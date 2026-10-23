#include "echo/app.hpp"
#include <algorithm>
#include <commctrl.h>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace echo {
namespace {
std::string text(HWND h) {
    std::wstring value(GetWindowTextLengthW(h) + 1, 0);
    GetWindowTextW(h, value.data(), int(value.size()));
    value.resize(wcslen(value.c_str()));
    int n =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), out.data(), n, nullptr, nullptr);
    return out;
}
float real(HWND h) {
    auto s = text(h);
    std::size_t used;
    float v = std::stof(s, &used);
    if (s.find_first_not_of(" \t\r\n", used) != std::string::npos || !std::isfinite(v))
        throw std::runtime_error("Enter a finite number");
    return v;
}
int integer(HWND h, int lo, int hi) {
    auto s = text(h);
    std::size_t used;
    long long v = std::stoll(s, &used);
    if (s.find_first_not_of(" \t\r\n", used) != std::string::npos || v < lo || v > hi)
        throw std::runtime_error("Integer is outside the displayed range");
    return int(v);
}
void platformControls(App& a) {
    int kind = int(SendMessageW(a.controls[ObjectKindSelect], CB_GETCURSEL, 0, 0));
    bool ordinary = kind != int(ObjectKind::Player) && kind != int(ObjectKind::Boss);
    EnableWindow(a.controls[ObjectRide], ordinary);
    EnableWindow(a.controls[ObjectCrush], ordinary);
}
} // namespace
void App::showObjectsEditor() {
    command(Edit);
    if (objectsEditor && IsWindow(objectsEditor)) {
        SetForegroundWindow(objectsEditor);
        return;
    }
    objectsEditor =
        CreateWindowW(L"EchoEngineWindow", L"对象模板与地图实例",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | ((verifyMode || smoke) ? 0 : WS_VISIBLE),
                      80, 60, 1240, 840, window, nullptr, instance, this);
    auto label = [&](const wchar_t* s, int x, int y, int w = 240) {
        control(L"STATIC", s, 0, x, y, w, 25, 0, objectsEditor);
    };
    auto edit = [&](int id, int x, int y, int w = 140) {
        control(L"EDIT", L"", id, x, y, w, 29, WS_BORDER | WS_TABSTOP, objectsEditor);
    };
    auto button = [&](int id, const wchar_t* s, int x, int y, int w = 140) {
        control(L"BUTTON", s, id, x, y, w, 30, WS_TABSTOP, objectsEditor);
    };
    auto combo = [&](int id, const std::vector<std::wstring>& choices, int x, int y, int w = 290) {
        auto h =
            control(L"COMBOBOX", L"", id, x, y, w, 240,
                    CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, objectsEditor);
        for (const auto& c : choices)
            SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(c.c_str()));
        SendMessageW(h, CB_SETITEMHEIGHT, -1, 25);
        SendMessageW(h, CB_SETCURSEL, 0, 0);
    };
    label(L"可复用模板", 20, 16, 260);
    control(L"LISTBOX", L"", ObjectList, 20, 48, 250, 437,
            WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, objectsEditor);
    combo(ObjectPresetSelect,
          {L"巡逻敌人", L"追击敌人", L"弹幕炮台", L"NPC", L"门 / 机关", L"拾取物", L"装饰", L"玩家",
           L"Boss"},
          20, 503, 250);
    button(ObjectNew, L"从预设新建", 20, 548, 250);
    button(ObjectCopy, L"复制模板", 20, 590, 120);
    button(ObjectDelete, L"删除模板", 150, 590, 120);
    label(L"模板属性 · 新试玩时应用", 300, 16, 450);
    label(L"名称（63 UTF-8 字节）", 300, 50);
    edit(ObjectName, 300, 78, 450);
    combo(ObjectKindSelect, {L"玩家", L"敌人", L"NPC", L"机关", L"拾取物", L"装饰", L"Boss"}, 300, 126,
          216);
    combo(ObjectBehaviorSelect, {L"待机", L"巡逻", L"追击", L"炮台", L"门", L"收集"}, 532, 126, 218);
    label(L"图片（空白使用类别图形）", 300, 167, 450);
    edit(ObjectSprite, 300, 195, 328);
    button(ObjectImport, L"导入图片", 640, 195, 110);
    struct Field {
        int id;
        const wchar_t* label;
    };
    Field fields[] = {{ObjectWidth, L"宽 · 1–2048 逻辑 px"},
                      {ObjectHeight, L"高 · 1–2048 逻辑 px"},
                      {ObjectSpeed, L"速度 · 0–10000 px/s"},
                      {ObjectHP, L"血量 · 1–1000000"},
                      {ObjectDamage, L"接触伤害 · 0–1000000"},
                      {ObjectRange, L"巡逻半径 · px"},
                      {ObjectDetect, L"发现半径 · px"},
                      {ObjectInterval, L"发射间隔 · 0.05–600 s"},
                      {ObjectCategory, L"碰撞分类位 · 1–65535"},
                      {ObjectMask, L"碰撞掩码 · 0–65535"},
                      {ObjectFaction, L"阵营 · 1玩家 / 2敌人"},
                      {ObjectPickupVariable, L"收集变量 · -1关闭 / 0–63"},
                      {ObjectPickupValue, L"收集增加值 · ±1000000"}};
    for (int i = 0; i < 13; ++i) {
        int x = 300 + (i % 2) * 232, y = 239 + (i / 2) * 62;
        label(fields[i].label, x, y, 218);
        if (fields[i].id == ObjectPickupVariable) {
            std::vector<std::wstring> vars{L"不增加变量"};
            for (int j = 0; j < 64; ++j)
                vars.push_back(wide(dataLabel(database, true, j)));
            combo(ObjectPickupVariable, vars, x, y + 25, 218);
        } else
            edit(fields[i].id, x, y + 25, 218);
    }
    button(ObjectGravity, L"使用重力", 532, 639, 106);
    button(ObjectSolid, L"实体碰撞", 646, 639, 104);
    button(ObjectRide, L"随平台移动", 300, 667, 216);
    button(ObjectCrush, L"平台挤压时死亡", 532, 667, 218);
    button(ObjectApply, L"应用模板", 300, 699, 216);
    button(ObjectPlace, L"在地图上放置 / 拖动", 532, 699, 218);
    label(L"当前地图实例 · 覆盖与持久化", 786, 16, 410);
    combo(ObjectInstances, {}, 786, 50, 410);
    label(L"位置 X / Y · 逻辑 px", 786, 103, 410);
    edit(ObjectX, 786, 132, 198);
    edit(ObjectY, 998, 132, 198);
    label(L"实例速度 / 血量 / 接触伤害", 786, 178, 410);
    edit(ObjectInstanceSpeed, 786, 207, 125);
    edit(ObjectInstanceHP, 928, 207, 125);
    edit(ObjectInstanceDamage, 1071, 207, 125);
    button(ObjectOverrideSpeed, L"独立速度", 786, 244, 125);
    button(ObjectOverrideHP, L"独立血量", 928, 244, 125);
    button(ObjectOverrideDamage, L"独立伤害", 1071, 244, 125);
    label(L"进入时请求地图事件", 786, 283, 410);
    combo(ObjectInstanceEvent, {L"不触发"}, 786, 312, 410);
    button(ObjectPersistent, L"重进地图保留状态（含已收集 / 已死亡）", 786, 358, 410);
    label(L"应用实例值后独立于模板；恢复继承即可跟随模板。", 786, 400, 410);
    button(ObjectInstanceApply, L"应用实例覆盖", 786, 446, 410);
    button(ObjectInherit, L"恢复模板继承", 786, 490, 410);
    button(ObjectInstanceDelete, L"删除地图实例", 786, 534, 410);
    label(L"左键放置 / 选择，拖动移动；右键删除。", 786, 590, 410);
    label(L"对象池 256；每地图最多 64 个实例。", 786, 624, 410);
    label(L"模板保存后可复用；运行时状态全部参与回溯。", 786, 658, 410);
    label(L"Ctrl+S 保存工程。删除被引用的模板会显示使用地图。", 20, 760, 1176);
    objectForm();
}
void App::objectForm(bool rebuild) {
    if (!objectsEditor)
        return;
    auto put = [&](int id, float v) { setNumber(id, v); };
    if (rebuild) {
        SendMessageW(controls[ObjectList], LB_RESETCONTENT, 0, 0);
        for (const auto& t : objectLibrary.templates) {
            auto name = wide(std::to_string(t.id) + "  " + t.name.c_str());
            SendMessageW(controls[ObjectList], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        SendMessageW(controls[ObjectList], LB_SETCURSEL, objectTemplateIndex, 0);
        SendMessageW(controls[ObjectInstances], CB_RESETCONTENT, 0, 0);
        for (const auto& p : editor.level.objects) {
            auto* t = findObjectTemplate(objectLibrary, p.templateId);
            auto name =
                wide("#" + std::to_string(p.id) + "  " + (t ? t->name.c_str() : "Missing template"));
            SendMessageW(controls[ObjectInstances], CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(name.c_str()));
        }
        SendMessageW(controls[ObjectInstances], CB_SETCURSEL, objectInstanceIndex, 0);
    }
    if (objectTemplateIndex >= 0 && objectTemplateIndex < int(objectLibrary.templates.size())) {
        const auto& t = objectLibrary.templates[objectTemplateIndex];
        SetWindowTextW(controls[ObjectName], wide(t.name.c_str()).c_str());
        SetWindowTextW(controls[ObjectSprite], wide(t.sprite.c_str()).c_str());
        SendMessageW(controls[ObjectKindSelect], CB_SETCURSEL, int(t.kind), 0);
        SendMessageW(controls[ObjectBehaviorSelect], CB_SETCURSEL, int(t.behavior), 0);
        for (auto [id, value] :
             std::vector<std::pair<int, float>>{{ObjectWidth, t.width},
                                                {ObjectHeight, t.height},
                                                {ObjectSpeed, t.speed},
                                                {ObjectHP, float(t.hp)},
                                                {ObjectDamage, float(t.damage)},
                                                {ObjectRange, t.patrolRange},
                                                {ObjectDetect, t.detectRange},
                                                {ObjectInterval, t.attackInterval},
                                                {ObjectCategory, float(t.category)},
                                                {ObjectMask, float(t.mask)},
                                                {ObjectFaction, float(t.faction)},

                                                {ObjectPickupValue, float(t.pickupValue)}})
            put(id, value);
        SendMessageW(controls[ObjectGravity], BM_SETCHECK, t.gravity, 0);
        SendMessageW(controls[ObjectSolid], BM_SETCHECK, t.solid, 0);
        SendMessageW(controls[ObjectRide], BM_SETCHECK, t.ridesPlatforms, 0);
        SendMessageW(controls[ObjectCrush], BM_SETCHECK, t.crushKills, 0);
        SendMessageW(controls[ObjectPickupVariable], CB_SETCURSEL, t.pickupVariable + 1, 0);
        editor.objectTemplate = t.id;
        platformControls(*this);
    }
    bool placed = objectInstanceIndex >= 0 && objectInstanceIndex < int(editor.level.objects.count);
    for (int id = ObjectX; id <= ObjectInstanceDelete; ++id)
        EnableWindow(controls[id], placed);
    if (placed) {
        const auto& p = editor.level.objects[objectInstanceIndex];
        auto* t = findObjectTemplate(objectLibrary, p.templateId);
        put(ObjectX, p.x);
        put(ObjectY, p.y);
        put(ObjectInstanceSpeed, (p.overrides & OverrideSpeed) || !t ? p.speed : t->speed);
        put(ObjectInstanceHP, float((p.overrides & OverrideHP) || !t ? p.hp : t->hp));
        put(ObjectInstanceDamage, float((p.overrides & OverrideDamage) || !t ? p.damage : t->damage));
        SendMessageW(controls[ObjectInstanceEvent], CB_RESETCONTENT, 0, 0);
        std::wstring none = L"不触发";
        SendMessageW(controls[ObjectInstanceEvent], CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(none.c_str()));
        SendMessageW(controls[ObjectInstanceEvent], CB_SETITEMDATA, 0, 0);
        int chosen = 0;
        for (const auto& event : editor.events.events) {
            auto name = wide(event.name.c_str());
            auto index = SendMessageW(controls[ObjectInstanceEvent], CB_ADDSTRING, 0,
                                      reinterpret_cast<LPARAM>(name.c_str()));
            SendMessageW(controls[ObjectInstanceEvent], CB_SETITEMDATA, index, event.id);
            if (event.id == p.eventId)
                chosen = int(index);
        }
        SendMessageW(controls[ObjectInstanceEvent], CB_SETCURSEL, chosen, 0);
        SendMessageW(controls[ObjectOverrideSpeed], BM_SETCHECK, (p.overrides & OverrideSpeed) != 0, 0);
        SendMessageW(controls[ObjectOverrideHP], BM_SETCHECK, (p.overrides & OverrideHP) != 0, 0);
        SendMessageW(controls[ObjectOverrideDamage], BM_SETCHECK, (p.overrides & OverrideDamage) != 0,
                     0);
        SendMessageW(controls[ObjectPersistent], BM_SETCHECK, p.persistent, 0);
    }
}
void App::objectCommand(int id, int notification) {
    if (id == ObjectsEditor) {
        showObjectsEditor();
        return;
    }
    if (!objectsEditor)
        return;
    if (id == ObjectList && notification == LBN_SELCHANGE) {
        objectTemplateIndex = int(SendMessageW(controls[id], LB_GETCURSEL, 0, 0));
        objectForm(false);
        return;
    }
    if (id == ObjectKindSelect && notification == CBN_SELCHANGE) {
        platformControls(*this);
        return;
    }
    if (id == ObjectInstances && notification == CBN_SELCHANGE) {
        objectInstanceIndex = selection(id);
        editor.selectedObject = objectInstanceIndex;
        if (objectInstanceIndex >= 0) {
            auto templateId = editor.level.objects[objectInstanceIndex].templateId;
            for (std::size_t i = 0; i < objectLibrary.templates.size(); ++i)
                if (objectLibrary.templates[i].id == templateId)
                    objectTemplateIndex = int(i);
        }
        objectForm();
        return;
    }
    if (id == ObjectNew || id == ObjectCopy || id == ObjectDelete || id == ObjectPlace ||
        id == ObjectImport || id == ObjectApply || id == ObjectInstanceApply || id == ObjectInherit ||
        id == ObjectInstanceDelete)
        command(Edit);
    if (id == ObjectNew || id == ObjectCopy) {
        if (objectLibrary.templates.size() >= 256)
            throw std::runtime_error("Object template capacity is 256");
        auto id = allocateObjectTemplateId(objectLibrary);
        auto t = objectPreset(id, selection(ObjectPresetSelect));
        if (id == ObjectCopy && objectTemplateIndex >= 0 &&
            objectTemplateIndex < int(objectLibrary.templates.size())) {
            t = objectLibrary.templates[objectTemplateIndex];
            t.id = id;
            t.name.set(std::string(t.name.c_str()).substr(0, 55) + " copy");
        }
        objectLibrary.templates.push_back(t);
        objectTemplateIndex = int(objectLibrary.templates.size()) - 1;
        editor.dirty = true;
        objectForm();
        return;
    }
    if (id == ObjectDelete) {
        if (objectTemplateIndex < 0 || objectTemplateIndex >= int(objectLibrary.templates.size()))
            return;
        auto target = objectLibrary.templates[objectTemplateIndex].id;
        std::string uses;
        for (std::size_t i = 0; i < maps.size(); ++i) {
            const auto& level = int(i) == editMap ? editor.level : maps[i];
            for (const auto& p : level.objects)
                if (p.templateId == target) {
                    uses += "\n" + config.maps[i] + " / #" + std::to_string(p.id);
                }
        }
        for (const auto& binding : animationLibrary.bindings)
            if (binding.templateId == objectLibrary.templates[objectTemplateIndex].id)
                uses += "\nAnimation state bindings";
        if (!uses.empty())
            throw std::runtime_error("Remove or reassign these instances before deleting the template:" +
                                     uses);
        objectLibrary.templates.erase(objectLibrary.templates.begin() + objectTemplateIndex);
        objectTemplateIndex =
            objectLibrary.templates.empty()
                ? -1
                : std::min(objectTemplateIndex, int(objectLibrary.templates.size()) - 1);
        editor.dirty = true;
        objectForm();
        return;
    }
    if (id == ObjectPlace) {
        if (objectTemplateIndex < 0 || objectTemplateIndex >= int(objectLibrary.templates.size()))
            return;
        editor.objectTemplate = objectLibrary.templates[objectTemplateIndex].id;
        editor.tool = 22;
        message = "Object tool: left click / drag, right click delete";
        SetFocus(canvas);
        return;
    }
    if (id == ObjectImport) {
        auto path = choose(false, L"Sprite images\0*.png;*.bmp\0\0", L"Import object sprite", L"png");
        if (path.empty())
            return;
        auto ref = importResource(path, "assets", AssetKind::Image);
        SetWindowTextW(controls[ObjectSprite], wide(ref).c_str());
        return;
    }
    if (id == ObjectApply) {
        if (objectTemplateIndex < 0 || objectTemplateIndex >= int(objectLibrary.templates.size()))
            return;
        auto next = objectLibrary;
        auto& t = next.templates[objectTemplateIndex];
        auto name = text(controls[ObjectName]), sprite = text(controls[ObjectSprite]);
        if (name.empty() || name.size() >= 64 || sprite.size() >= 256)
            throw std::runtime_error("Object name or sprite reference is too long");
        if (!sprite.empty() && !resources.contains(sprite))
            throw std::runtime_error("Missing object sprite");
        t.name.set(name);
        t.sprite.set(sprite);
        t.kind = ObjectKind(selection(ObjectKindSelect));
        t.behavior = ObjectBehavior(selection(ObjectBehaviorSelect));
        t.width = real(controls[ObjectWidth]);
        t.height = real(controls[ObjectHeight]);
        t.speed = real(controls[ObjectSpeed]);
        t.hp = integer(controls[ObjectHP], 1, 1000000);
        t.damage = integer(controls[ObjectDamage], 0, 1000000);
        t.patrolRange = real(controls[ObjectRange]);
        t.detectRange = real(controls[ObjectDetect]);
        t.attackInterval = real(controls[ObjectInterval]);
        t.category = std::uint16_t(integer(controls[ObjectCategory], 1, 65535));
        t.mask = std::uint16_t(integer(controls[ObjectMask], 0, 65535));
        t.faction = std::uint16_t(integer(controls[ObjectFaction], 0, 65535));
        t.pickupVariable = selection(ObjectPickupVariable) - 1;
        t.pickupValue = integer(controls[ObjectPickupValue], -1000000, 1000000);
        t.gravity = SendMessageW(controls[ObjectGravity], BM_GETCHECK, 0, 0) == BST_CHECKED;
        t.solid = SendMessageW(controls[ObjectSolid], BM_GETCHECK, 0, 0) == BST_CHECKED;
        t.ridesPlatforms = SendMessageW(controls[ObjectRide], BM_GETCHECK, 0, 0) == BST_CHECKED;
        t.crushKills = SendMessageW(controls[ObjectCrush], BM_GETCHECK, 0, 0) == BST_CHECKED;
        validateObjects(next);
        // Preload sprite resources at an editor control boundary, never on a gameplay tick.
        auto previous = resources;
        resources.setEditorOverlay({{"objects.echo", saveObjects(next)},
                                    {"animations.echo", saveAnimations(animationLibrary)},
                                    {"barrage.echo", saveBarrage(editor.barrage)},
                                    {"assets.echo", saveAssets(assetLibrary)},
                                    {"project.echo", projectText()}});
        try {
            renderer.setResources(&resources);
        } catch (...) {
            resources = std::move(previous);
            throw;
        }
        resources = std::move(previous);
        objectLibrary = std::move(next);
        for (std::size_t m = 0; m < maps.size(); ++m) {
            auto& level = int(m) == editMap ? editor.level : maps[m];
            for (auto& p : level.objects)
                if (p.templateId == t.id && !(p.overrides & OverrideSize)) {
                    p.width = t.width;
                    p.height = t.height;
                }
        }
        editor.dirty = true;
        objectForm();
        message = "Object template applied";
        return;
    }
    if (id != ObjectInstanceApply && id != ObjectInherit && id != ObjectInstanceDelete)
        return;
    if (objectInstanceIndex < 0 || objectInstanceIndex >= int(editor.level.objects.count))
        return;
    auto p = editor.level.objects[objectInstanceIndex];
    if (id == ObjectInstanceApply) {
        p.x = real(controls[ObjectX]);
        p.y = real(controls[ObjectY]);
        p.speed = real(controls[ObjectInstanceSpeed]);
        p.hp = integer(controls[ObjectInstanceHP], 1, 1000000);
        p.damage = integer(controls[ObjectInstanceDamage], 0, 1000000);
        p.eventId = int(SendMessageW(controls[ObjectInstanceEvent], CB_GETITEMDATA,
                                     selection(ObjectInstanceEvent), 0));
        if (p.eventId && !findEvent(editor.events, p.eventId))
            throw std::runtime_error("Create the target map event first");
        if (p.speed < 0 || p.speed > 10000)
            throw std::runtime_error("Instance speed must be 0..10000");
        p.persistent = SendMessageW(controls[ObjectPersistent], BM_GETCHECK, 0, 0) == BST_CHECKED;
        p.overrides &= OverrideSize;
        if (SendMessageW(controls[ObjectOverrideSpeed], BM_GETCHECK, 0, 0) == BST_CHECKED)
            p.overrides |= OverrideSpeed;
        if (SendMessageW(controls[ObjectOverrideHP], BM_GETCHECK, 0, 0) == BST_CHECKED)
            p.overrides |= OverrideHP;
        if (SendMessageW(controls[ObjectOverrideDamage], BM_GETCHECK, 0, 0) == BST_CHECKED)
            p.overrides |= OverrideDamage;
    } else if (id == ObjectInherit) {
        p.overrides = 0;
        auto* t = findObjectTemplate(objectLibrary, p.templateId);
        if (t) {
            p.width = t->width;
            p.height = t->height;
        }
    }
    editor.begin();
    if (id == ObjectInstanceDelete) {
        editor.level.objects.erase(objectInstanceIndex);
        objectInstanceIndex = editor.selectedObject = -1;
    } else
        editor.level.objects[objectInstanceIndex] = p;
    editor.dirty = true;
    editor.end();
    objectForm();
}
} // namespace echo

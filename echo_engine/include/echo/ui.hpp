#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>
// Echo's own retained UI toolkit: anti-aliased shapes and vector icons rasterized into a DIB,
// GDI ClearType text, and a node list rebuilt from editor state. No native controls, no ImGui.
namespace echo::ui {
struct Color {
    std::uint8_t r{}, g{}, b{}, a = 255;
};
constexpr Color hex(std::uint32_t v, std::uint8_t a = 255) {
    return {std::uint8_t(v >> 16), std::uint8_t(v >> 8), std::uint8_t(v), a};
}
Color mix(Color a, Color b, float t);
Color alpha(Color c, float a);
COLORREF colorref(Color c);

struct Box {
    int x{}, y{}, w{}, h{};
    int right() const;
    int bottom() const;
    bool contains(int px, int py) const;
    Box inset(int dx, int dy) const;
    Box intersect(const Box&) const;
};

struct Theme {
    bool dark{};
    Color window, chrome, panel, sunken, raised, border, borderStrong, divider;
    Color text, muted, faint, hover, pressed;
    Color accent, accentHover, accentPressed, accentSoft, accentText, onAccent;
    Color play, playHover, playPressed, stop, stopHover, stopPressed, warn;
    Color tooltip, tooltipText, shadow, backdrop;
};
const Theme& theme(bool dark);

// Device pixels for a 96-DPI design value.
int dp(float value, float scale);
float systemScale();

struct Fonts {
    HFONT body{}, small{}, strong{}, title{}, mark{};
    float scale = 1;
    void create(float scale);
    void destroy();
    ~Fonts();
};

enum class Icon : std::uint8_t {
    None,
    New,
    Open,
    Save,
    Undo,
    Redo,
    Terrain,
    Event,
    Object,
    Barrage,
    Feature,
    Pencil,
    Rectangle,
    Bucket,
    Select,
    Picker,
    Sliders,
    Database,
    Play,
    Stop,
    Sun,
    Moon,
    Plus,
    Map,
    Folder,
    Import,
    Edit,
    ChevronDown,
    ChevronRight,
    Check,
    Dot,
    Flag,
    Close
};

class Painter {
  public:
    Painter() = default;
    Painter(const Painter&) = delete;
    Painter& operator=(const Painter&) = delete;
    ~Painter();
    void resize(int width, int height);
    HDC dc() const;
    int width() const;
    int height() const;
    std::uint32_t* pixels();
    void clip(Box);
    void unclip();
    Box clipBox() const;
    void fill(Box, Color);
    // Anti-aliased rounded rectangle, optional inner stroke.
    void round(Box, float radius, Color fill, Color stroke = {0, 0, 0, 0}, float strokeWidth = 1);
    void circle(float cx, float cy, float r, Color);
    void ring(float cx, float cy, float r, float width, Color);
    void line(float x0, float y0, float x1, float y1, float width, Color);
    void polyline(const float* xy, int points, float width, Color, bool closed = false);
    void polygon(const float* xy, int points, Color); // convex only
    void crescent(float cx, float cy, float r, float cutX, float cutY, float cutR, Color);
    void text(Box, std::wstring_view, HFONT, Color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int measure(std::wstring_view, HFONT);
    void image(HBITMAP, int sx, int sy, int sw, int sh, Box dst);
    void icon(Icon, Box, Color);
    void present(HDC target, Box area, int targetX, int targetY);

  private:
    void flush();
    void plot(int x, int y, Color, float coverage);
    template <class Distance> void shape(float x0, float y0, float x1, float y1, Color, Distance);
    HDC dc_{};
    HBITMAP bitmap_{}, old_{};
    std::uint32_t* pixels_{};
    int width_{}, height_{};
    Box clip_{};
    bool gdi_{};
};

enum class Kind : std::uint8_t {
    Fill,
    Divider,
    Label,
    Button,
    Segment,
    Row,
    MenuTitle,
    Badge,
    Splitter,
    Toggle,   // label + switch; Selected = on
    Slider,   // label, value text (note) and track; number within [minimum, maximum]
    Dropdown, // label + field showing note; caller opens a popup menu
    Section,  // property group heading
    Custom
};
// Node flags.
inline constexpr std::uint32_t Selected = 1u << 0;
inline constexpr std::uint32_t Disabled = 1u << 1;
inline constexpr std::uint32_t Primary = 1u << 2;
inline constexpr std::uint32_t Positive = 1u << 3; // green "run" button
inline constexpr std::uint32_t Negative = 1u << 4; // red "stop" button
inline constexpr std::uint32_t Quiet = 1u << 5;    // no border until hovered
inline constexpr std::uint32_t Strong = 1u << 6;
inline constexpr std::uint32_t Small = 1u << 7;
inline constexpr std::uint32_t Muted = 1u << 8;
inline constexpr std::uint32_t Faint = 1u << 9;
inline constexpr std::uint32_t Center = 1u << 10;
inline constexpr std::uint32_t Right = 1u << 11;
inline constexpr std::uint32_t Wrap = 1u << 12;
inline constexpr std::uint32_t Accent = 1u << 13;
inline constexpr std::uint32_t Warn = 1u << 14;
inline constexpr std::uint32_t Mark = 1u << 15;  // little status dot on an icon button
inline constexpr std::uint32_t Header = 1u << 16; // bold root rows
struct Node {
    Kind kind{};
    Box box{};
    std::wstring text, note, badge, tip;
    Icon icon{};
    int action{}, value{};
    std::uint32_t flags{};
    int radius{};
    Color color{0, 0, 0, 0}, stroke{0, 0, 0, 0};
    int scroll = -1, indent{};
    float number{}, minimum{}, maximum = 1;
};
// Geometry shared by painting and hit handling.
Box sliderTrack(Box row, float scale);
float sliderValue(const Node&, Box row, int x, float scale);
Box dropdownField(Box row, float scale);
struct Key {
    int action{}, value{};
    bool operator==(const Key&) const = default;
};
struct ScrollArea {
    int id{};
    Box box{};
    int content{}, step{};
};

class View {
  public:
    std::vector<Node> nodes;
    std::vector<ScrollArea> areas;
    std::map<int, int> offsets;  // survives rebuilds
    std::map<int, int> revealed; // last selection scrolled into view, per area
    // Scrolls `area` so a row is visible, but only when `selected` changed since the last call,
    // so the user can still scroll away from the current selection.
    void reveal(int area, int selected, int rowTop, int rowHeight);
    Key hot{}, pressed{};
    void clear();
    Node& add(Node);
    int scrollArea(int id, Box box, int step);
    void finishArea(int index, int contentBottom);
    const Node* hit(int x, int y, bool disabledToo = false) const; // disabledToo: hover / tooltips
    const Node* find(int action, int value) const;
    Box screenBox(const Node&) const; // box after scrolling
    bool wheel(int x, int y, int delta);
    using CustomPaint = std::function<void(Painter&, const Node&, Box)>;
    void paint(Painter&, const Theme&, const Fonts&, const CustomPaint& custom = {}) const;

  private:
    int offset(int area) const;
    void paintNode(Painter&, const Theme&, const Fonts&, const Node&, Box, const CustomPaint&) const;
};

// Skins for native controls that remain in older dialogs; `surface` is the colour behind the
// control (card or window), used for the anti-aliased corners.
struct ControlState {
    bool hovered{}, pressed{}, active{}, primary{}, disabled{}, focused{}, checked{};
};
void paintPushButton(Painter&, Box, std::wstring_view text, HFONT, const Theme&, Color surface,
                     const ControlState&);
void paintCheckBox(Painter&, Box, std::wstring_view text, HFONT, const Theme&, Color surface,
                   const ControlState&);
void paintComboFace(Painter&, Box, std::wstring_view text, HFONT, const Theme&, Color surface,
                    const ControlState&);
void paintFieldFrame(Painter&, Box, const Theme&, Color surface, const ControlState&, float radius = 6);
// Rows inside a list or drop-down; `fill` matches the list's background.
void paintListItem(Painter&, Box, std::wstring_view text, HFONT, const Theme&, const ControlState&, Color fill);
// Fields read as inset on cards and as raised on the window background.
Color fieldFill(const Theme&, Color surface);

struct MenuItem {
    std::wstring text, shortcut;
    int command{};
    bool checked{}, disabled{}, separator{};
};
// Measures and paints a popup menu body into `p` at the origin (used by popups and previews).
Box menuSize(Painter& p, const std::vector<MenuItem>&, const Fonts&);
void paintMenu(Painter& p, const std::vector<MenuItem>&, int hot, const Theme&, const Fonts&);
// Modal custom popup menu. Returns the chosen command or 0. While open, `menuBarHit` maps a
// screen point to another menu-bar entry; moving onto one closes this menu and stores it in
// `*switchTo` (also used for Left / Right arrows with -2 / -3).
int popupMenu(HWND owner, POINT screen, const std::vector<MenuItem>&, bool dark, const Fonts&,
              const std::function<int(POINT)>& menuBarHit = {}, int current = -1,
              int* switchTo = nullptr, bool keyboard = false);

class Tooltip {
  public:
    ~Tooltip();
    void show(HWND owner, POINT screen, const std::wstring& text, bool dark, const Fonts&);
    void hide();
    bool visible() const;

  private:
    HWND window_{};
    std::wstring text_;
};
} // namespace echo::ui

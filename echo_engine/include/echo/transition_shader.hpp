#pragma once
namespace echo {
inline constexpr const char* transitionPrelude = R"GLSL(#version 120
uniform float u_cover;
uniform float u_detail;
uniform int u_kind;
uniform int u_direction;
uniform int u_phase;
uniform vec2 u_center;
uniform vec2 u_resolution;
uniform vec4 u_color;
)GLSL";
inline constexpr const char* transitionBuiltins = R"GLSL(
float echo_transition(vec2 uv, float cover) {
    vec2 p=uv*u_resolution, center=u_center*u_resolution;
    if(u_kind==1) return cover;
    if(u_kind==2) {
        vec2 far=max(center,u_resolution-center);
        return step(length(far)*(1.0-cover),length(p-center));
    }
    if(u_kind==3) {
        vec2 far=max(center,u_resolution-center);
        vec2 delta=abs(p-center);
        return step((far.x+far.y)*(1.0-cover),delta.x+delta.y);
    }
    if(u_kind==4) {
        float v=u_direction<2?uv.x:uv.y;
        return step(1.0-cover,abs(v*2.0-1.0));
    }
    float axis=uv.x;
    if(u_direction==1) axis=1.0-uv.x;
    if(u_direction==2) axis=uv.y;
    if(u_direction==3) axis=1.0-uv.y;
    if(u_kind==5) {
        float secondary=u_direction<2?uv.y:uv.x;
        float edge=(axis+secondary*.35)/1.35;
        return 1.0-step(cover,edge);
    }
    vec2 cell=floor(p/max(1.0,u_detail));
    float hash=fract(sin(dot(cell,vec2(12.9898,78.233)))*43758.5453);
    return 1.0-step(cover,hash*.7+axis*.3);
}
)GLSL";
inline constexpr const char* transitionPostlude = R"GLSL(
void main() {
    // Full coverage at commit is enforced even for author-supplied mask functions.
    float a = u_cover <= 0.0 ? 0.0 : u_cover >= 1.0 ? 1.0 :
        clamp(echo_transition(vec2(gl_FragCoord.x/u_resolution.x,
                                  1.0-gl_FragCoord.y/u_resolution.y),u_cover),0.0,1.0);
    gl_FragColor=vec4(u_color.rgb,a);
}
)GLSL";
} // namespace echo

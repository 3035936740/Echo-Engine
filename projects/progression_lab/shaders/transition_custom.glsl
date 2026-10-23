// Echo Engine custom transition mask: GLSL 1.20 function, no main/#version.
// uv is top-left origin, normalized; cover 0=visible, 1=fully covered.
// Uniforms: u_center, u_resolution, u_direction, u_detail, u_phase.
// Engine enforces completely visible/covered endpoints. Never affect gameplay.
float echo_transition(vec2 uv, float cover) {
    vec2 p=(uv-u_center)*vec2(1.7777778,1.0);
    float angle=atan(p.y,p.x);
    float ripple=1.0+0.15*cos(angle*6.0);
    float radius=(1.0-cover)*1.1*ripple;
    return step(radius,length(p));
}

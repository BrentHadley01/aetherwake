#version 120

uniform mat4 uInverseView;
uniform int uMode;
uniform float uTime;

varying vec3 vNormal;
varying vec3 vViewPosition;
varying vec2 vUv;
varying vec4 vTint;
varying vec3 vWorld;

void main() {
    vec4 vertex = gl_Vertex;
    if (uMode == 3) {
        // Grass sway: texcoord.x is the bend weight (0 at the root, 1 at the tip).
        float sway = sin(uTime * 1.9 + vertex.x * 0.35 + vertex.z * 0.28) * 0.09 * gl_MultiTexCoord0.x;
        vertex.x += sway;
        vertex.z += sway * 0.6;
    }
    vec4 viewPosition = gl_ModelViewMatrix * vertex;
    vViewPosition = viewPosition.xyz;
    vNormal = normalize(gl_NormalMatrix * gl_Normal);
    vUv = gl_MultiTexCoord0.xy;
    vTint = gl_Color;
    // Reconstruct true world space so node-local GLB meshes fog and shadow correctly.
    vWorld = (uInverseView * viewPosition).xyz;
    gl_Position = gl_ProjectionMatrix * viewPosition;
}

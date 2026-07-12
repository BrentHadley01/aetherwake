#version 120

uniform mat4 uInverseView;

varying vec3 vNormal;
varying vec3 vViewPosition;
varying vec2 vUv;
varying vec4 vTint;
varying vec3 vWorld;

void main() {
    vec4 viewPosition = gl_ModelViewMatrix * gl_Vertex;
    vViewPosition = viewPosition.xyz;
    vNormal = normalize(gl_NormalMatrix * gl_Normal);
    vUv = gl_MultiTexCoord0.xy;
    vTint = gl_Color;
    // Reconstruct true world space so node-local GLB meshes fog and shadow correctly.
    vWorld = (uInverseView * viewPosition).xyz;
    gl_Position = gl_ProjectionMatrix * viewPosition;
}

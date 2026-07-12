#version 120

uniform mat4 uInverseView;
uniform int uMode;
uniform float uTime;
uniform vec3 uPlayer;

varying vec3 vNormal;
varying vec3 vViewPosition;
varying vec2 vUv;
varying vec4 vTint;
varying vec3 vWorld;

void main() {
    vec4 vertex = gl_Vertex;
    if (uMode == 3) {
        // x carries root-to-tip bend weight; y is authored flexibility.
        float weight = gl_MultiTexCoord0.x;
        float flexibility = gl_MultiTexCoord0.y;
        float gust = sin(uTime * 0.43 + vertex.x * 0.055 - vertex.z * 0.038) * 0.5 + 0.5;
        vec2 windDirection = normalize(vec2(
            sin(vertex.x * 0.071 + vertex.z * 0.043 + 1.7),
            cos(vertex.x * 0.037 - vertex.z * 0.063 - 0.4)));
        float fineSway = sin(uTime * 1.9 + vertex.x * 0.35 + vertex.z * 0.28) * 0.075
                       + sin(uTime * 3.7 + vertex.x * 0.17 - vertex.z * 0.22) * 0.032;
        vertex.xz += windDirection * fineSway * (0.55 + gust * 0.85) * weight * flexibility;

        // Contact response: blades bend radially away from the character and
        // settle downward near the boots. Smooth falloff avoids a visible ring.
        vec2 playerDelta = vertex.xz - uPlayer.xz;
        float playerDistance = length(playerDelta);
        vec2 pushDirection = playerDelta / max(playerDistance, 0.08);
        float contact = (1.0 - smoothstep(0.28, 1.55, playerDistance)) * weight * weight;
        vertex.xz += pushDirection * contact * (0.72 + flexibility * 0.42);
        vertex.y -= contact * 0.24;
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

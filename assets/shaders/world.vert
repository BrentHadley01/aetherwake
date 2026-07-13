#version 120

uniform mat4 uInverseView;
uniform int uMode;
uniform float uTime;
uniform vec3 uPlayer;
// x = locomotion phase, y = movement strength, z = casting strength.
uniform vec3 uCharacterAnim;

varying vec3 vNormal;
varying vec3 vViewPosition;
varying vec2 vUv;
varying vec4 vTint;
varying vec3 vWorld;
varying vec3 vMaterial;

void main() {
    vec4 vertex = gl_Vertex;
    if (uMode == 5) {
        float phase = uCharacterAnim.x;
        float movement = uCharacterAnim.y;
        float casting = uCharacterAnim.z;
        float side = vertex.x < 0.0 ? -1.0 : 1.0;

        // Soft positional skinning for the normalized Tripo base meshes.
        // The characters share height/orientation, so the same locomotion
        // controller drives both while keeping their authored topology intact.
        float legWeight = (1.0 - smoothstep(0.76, 0.98, vertex.y)) * smoothstep(0.025, 0.11, abs(vertex.x));
        float legAngle = sin(phase + (side > 0.0 ? 0.0 : 3.14159265)) * 0.48 * movement;
        vec2 leg = vertex.yz - vec2(0.94, 0.0);
        vec2 legRotated = vec2(leg.x * cos(legAngle) - leg.y * sin(legAngle), leg.x * sin(legAngle) + leg.y * cos(legAngle));
        vertex.yz = mix(vertex.yz, legRotated + vec2(0.94, 0.0), legWeight);

        float armWeight = smoothstep(0.20, 0.43, abs(vertex.x)) * smoothstep(0.82, 1.30, vertex.y);
        float armAngle = sin(phase + (side > 0.0 ? 3.14159265 : 0.0)) * 0.54 * movement;
        // Casting leads with the right arm while the left counterbalances.
        armAngle += casting * (side > 0.0 ? -0.92 : 0.24);
        vec2 arm = vertex.yz - vec2(1.46, 0.0);
        vec2 armRotated = vec2(arm.x * cos(armAngle) - arm.y * sin(armAngle), arm.x * sin(armAngle) + arm.y * cos(armAngle));
        vertex.yz = mix(vertex.yz, armRotated + vec2(1.46, 0.0), armWeight);

        // Breathing and weight transfer prevent the idle pose from freezing.
        float breath = sin(uTime * 1.65) * 0.008 * smoothstep(1.05, 1.72, vertex.y);
        vertex.z += breath;
        vertex.y += abs(sin(phase * 2.0)) * 0.022 * movement;
    }
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
    vMaterial = gl_MultiTexCoord1.xyz;
    // Reconstruct true world space so node-local GLB meshes fog and shadow correctly.
    vWorld = (uInverseView * viewPosition).xyz;
    // Grass shades from its STABLE pre-sway position (blades are emitted in
    // world space, so gl_Vertex is world space). Animated vertices sampling
    // the shadow map at their displaced positions strobe light/dark whenever
    // a blade sways across a shadow edge; anchoring to the root keeps each
    // blade's shadowing constant while it moves.
    if (uMode == 3) vWorld = gl_Vertex.xyz;
    gl_Position = gl_ProjectionMatrix * viewPosition;
}

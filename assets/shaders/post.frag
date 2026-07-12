#version 120

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform int uPass;      // 0 = bright extract, 1 = blur H, 2 = blur V, 3 = composite
uniform vec3 uPixel;    // xy = source texel size
uniform float uTime;

varying vec2 vUv;

float ditherHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    if (uPass == 0) {
        // Bright extract into the quarter-res chain; bilinear does the downsample.
        vec3 color = texture2D(uScene, vUv).rgb;
        float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
        gl_FragColor = vec4(color * smoothstep(0.46, 0.80, luminance), 1.0);
    } else if (uPass == 1 || uPass == 2) {
        vec2 direction = uPass == 1 ? vec2(uPixel.x, 0.0) : vec2(0.0, uPixel.y);
        vec3 sum = texture2D(uScene, vUv).rgb * 0.227027;
        sum += (texture2D(uScene, vUv + direction * 1.3846).rgb + texture2D(uScene, vUv - direction * 1.3846).rgb) * 0.3162162;
        sum += (texture2D(uScene, vUv + direction * 3.2308).rgb + texture2D(uScene, vUv - direction * 3.2308).rgb) * 0.0702703;
        gl_FragColor = vec4(sum, 1.0);
    } else {
        vec3 scene = texture2D(uScene, vUv).rgb;
        vec3 bloom = texture2D(uBloom, vUv).rgb;
        // Small-radius local contrast restores photoscan and needle detail
        // softened by MSAA and bloom, without ringing the sky silhouette.
        vec3 neighbours = (texture2D(uScene, vUv + vec2(uPixel.x, 0.0)).rgb
                         + texture2D(uScene, vUv - vec2(uPixel.x, 0.0)).rgb
                         + texture2D(uScene, vUv + vec2(0.0, uPixel.y)).rgb
                         + texture2D(uScene, vUv - vec2(0.0, uPixel.y)).rgb) * 0.25;
        float edgeGuard = smoothstep(0.015, 0.16, length(scene - neighbours));
        vec3 color = scene + (scene - neighbours) * mix(0.10, 0.22, edgeGuard) + bloom * 0.58;
        // Slight saturation lift, then a soft vignette to pull focus inward.
        float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
        color = mix(vec3(luminance), color, 1.055);
        // Cool shadows and gently warm highlights separate depth planes while
        // keeping the grade physically restrained.
        float highlight = smoothstep(0.28, 0.82, luminance);
        color *= mix(vec3(0.96, 1.00, 1.045), vec3(1.035, 1.012, 0.975), highlight);
        vec2 centered = vUv - 0.5;
        color *= 1.0 - 0.22 * smoothstep(0.30, 0.90, dot(centered, centered) * 2.6);
        // Debanding dither: the night sky gradients band badly at 8 bits.
        color += (ditherHash(gl_FragCoord.xy + fract(uTime) * 61.0) - 0.5) * (2.0 / 255.0);
        gl_FragColor = vec4(color, 1.0);
    }
}

#version 120

uniform sampler2D uAlbedo;
uniform sampler2D uRock;
uniform sampler2DShadow uShadow;
uniform mat4 uLight;    // world -> shadow-map texture space
uniform int uShadowOn;
uniform int uMode;      // 0 = authored mesh, 1 = streamed terrain, 2 = water, 3 = grass, 4 = sky
uniform float uTime;
uniform vec3 uEye;

varying vec3 vNormal;
varying vec3 vViewPosition;
varying vec2 vUv;
varying vec4 vTint;
varying vec3 vWorld;

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float valueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1, 0)), f.x), mix(hash21(i + vec2(0, 1)), hash21(i + vec2(1, 1)), f.x), f.y);
}
float cloudFbm(vec2 p) {
    float sum = 0.0, amplitude = 0.5;
    for (int i = 0; i < 4; ++i) { sum += valueNoise(p) * amplitude; p = p * 2.13 + 7.7; amplitude *= 0.5; }
    return sum;
}

void main() {
    if (uMode == 4) {
        // Sky dome: authored gradient in vTint plus drifting moonlit clouds.
        vec3 direction = normalize(vWorld - uEye);
        vec3 result = vTint.rgb;
        if (direction.y > 0.02) {
            vec2 cloudUv = direction.xz / max(direction.y, 0.10) * 0.42 + vec2(uTime * 0.006, uTime * 0.0015);
            float broadCloud = cloudFbm(cloudUv);
            float wisps = cloudFbm(cloudUv * 2.15 + vec2(19.0, -7.0));
            float cloud = smoothstep(0.51, 0.72, broadCloud * 0.74 + wisps * 0.26);
            float horizonFade = smoothstep(0.03, 0.2, direction.y);
            float moonGlow = pow(max(dot(direction, normalize(vec3(-0.35, 0.72, 0.48))), 0.0), 16.0);
            vec3 cloudColor = mix(vec3(0.12, 0.145, 0.18), vec3(0.20, 0.225, 0.26), wisps);
            cloudColor += moonGlow * vec3(0.28, 0.31, 0.36);
            result = mix(result, cloudColor, cloud * horizonFade * 0.68);
            result += moonGlow * 0.05 * (1.0 - cloud);
        }
        gl_FragColor = vec4(result, 1.0);
        return;
    }
    vec3 normal = normalize(vNormal);
    vec3 lightDirection = normalize(vec3(-0.35, 0.72, 0.48));
    vec3 viewDirection = normalize(-vViewPosition);

    vec3 albedo;
    float specularStrength = 0.02;
    float shininess = 64.0;
    float alpha = 1.0;

    if (uMode == 1) {
        vec2 uv = vWorld.xz * 0.11;
        vec3 soilNear = texture2D(uAlbedo, uv).rgb;
        vec3 soilFar = texture2D(uAlbedo, uv * 0.171).rgb;   // second scale hides tiling
        vec3 soil = pow(mix(soilNear, soilFar, 0.45), vec3(2.2));   // sRGB -> linear
        vec3 rock = pow(mix(texture2D(uRock, vWorld.xz * 0.06).rgb, texture2D(uRock, vWorld.xz * 0.013).rgb, 0.4), vec3(2.2));
        // Micro-relief: treat the albedo as a heightfield and perturb the
        // normal so moonlight rakes across ground detail; fades with distance.
        float detailFade = exp(-length(vViewPosition) * 0.02);
        if (detailFade > 0.02) {
            float lumHere = dot(soilNear, vec3(0.333));
            float lumX = dot(texture2D(uAlbedo, uv + vec2(0.006, 0.0)).rgb, vec3(0.333));
            float lumZ = dot(texture2D(uAlbedo, uv + vec2(0.0, 0.006)).rgb, vec3(0.333));
            normal = normalize(normal + vec3(lumHere - lumX, 0.0, lumHere - lumZ) * 2.4 * detailFade);
        }
        float slope = 1.0 - normal.y;
        float rockBlend = smoothstep(0.16, 0.40, slope);
        vec3 ground = mix(soil * vTint.rgb, rock, rockBlend);
        float high = smoothstep(34.0, 54.0, vWorld.y);       // pale frost-dusted summits
        ground = mix(ground, rock * vec3(1.35, 1.42, 1.52), high * (1.0 - rockBlend * 0.35));
        float wet = smoothstep(-5.6, -7.6, vWorld.y);        // dark saturated shoreline
        albedo = mix(ground, ground * vec3(0.40, 0.39, 0.35), wet);
        specularStrength = 0.03 + 0.25 * wet;
    } else if (uMode == 2) {
        // Noise-perturbed phases keep the ripples from forming periodic
        // contour bands at glancing angles.
        float phase = valueNoise(vWorld.xz * 0.11) * 5.5;
        float w1 = sin(vWorld.x * 0.9 + phase + uTime * 1.3) + sin(vWorld.z * 0.7 - phase * 0.7 - uTime * 1.1);
        float w2 = sin((vWorld.x + vWorld.z) * 0.23 + phase + uTime * 0.6);
        float w3 = valueNoise(vWorld.xz * 0.35 + vec2(uTime * 0.05, -uTime * 0.04)) - 0.5;
        float rippleFade = exp(-length(vViewPosition) * 0.012);   // avoid distant moire
        normal = normalize(vec3((w1 * 0.03 + w2 * 0.04 + w3 * 0.10) * rippleFade, 1.0, (w1 * 0.035 - w2 * 0.04 + w3 * 0.09) * rippleFade));
        albedo = vec3(0.006, 0.026, 0.034);
        specularStrength = 0.9;
        shininess = 130.0;
        alpha = 0.86;
    } else if (uMode == 3) {
        albedo = vTint.rgb;   // grass blades carry their albedo as vertex color
        specularStrength = 0.015;
    } else {
        albedo = pow(texture2D(uAlbedo, vUv).rgb, vec3(2.2)) * vTint.rgb;
    }

    float shadow = 1.0;
    if (uShadowOn == 1) {
        vec4 shadowCoord = uLight * vec4(vWorld, 1.0);
        if (shadowCoord.x > 0.003 && shadowCoord.x < 0.997 && shadowCoord.y > 0.003 && shadowCoord.y < 0.997) {
            vec2 texel = vec2(1.0 / 2048.0);
            shadow = 0.0;
            shadow += shadow2D(uShadow, shadowCoord.xyz + vec3(-0.7 * texel.x, -0.7 * texel.y, 0.0)).r;
            shadow += shadow2D(uShadow, shadowCoord.xyz + vec3( 0.7 * texel.x, -0.7 * texel.y, 0.0)).r;
            shadow += shadow2D(uShadow, shadowCoord.xyz + vec3(-0.7 * texel.x,  0.7 * texel.y, 0.0)).r;
            shadow += shadow2D(uShadow, shadowCoord.xyz + vec3( 0.7 * texel.x,  0.7 * texel.y, 0.0)).r;
            shadow *= 0.25;
        }
    }

    vec3 halfVector = normalize(lightDirection + viewDirection);
    float diffuse = max(dot(normal, lightDirection), 0.0) * shadow;
    float specular = pow(max(dot(normal, halfVector), 0.0), shininess) * specularStrength * shadow;
    vec3 coolAmbient = vec3(0.022, 0.032, 0.048);
    vec3 skyBounce = vec3(0.012, 0.022, 0.034) * max(normal.y, 0.0);
    vec3 moonLight = vec3(0.62, 0.80, 0.95) * diffuse;
    vec3 color = albedo * (coolAmbient + skyBounce + moonLight) + specular * vec3(0.72, 0.9, 1.0);

    if (uMode == 2) {
        // Fresnel-weighted reflection of the synthesized night sky, so the
        // lake mirrors the gradient, drifting clouds, and a moon glint.
        vec3 incident = normalize(vWorld - uEye);
        vec3 reflected = reflect(incident, normal);
        reflected.y = max(abs(reflected.y), 0.02);
        float skyT = pow(clamp(reflected.y, 0.0, 1.0), 0.62);
        vec3 skyReflection = mix(vec3(0.050, 0.080, 0.105), vec3(0.007, 0.015, 0.030), skyT);
        vec2 reflectionUv = reflected.xz / reflected.y * 0.42 + vec2(uTime * 0.006, uTime * 0.0015);
        float cloud = smoothstep(0.51, 0.72, cloudFbm(reflectionUv) * 0.74 + cloudFbm(reflectionUv * 2.15 + vec2(19.0, -7.0)) * 0.26);
        skyReflection = mix(skyReflection, vec3(0.020, 0.026, 0.034), cloud * 0.85);
        float fresnel = 0.15 + 0.85 * pow(1.0 - max(dot(-incident, normal), 0.0), 3.0);
        color = mix(color, skyReflection, fresnel);
        color += pow(max(dot(reflected, lightDirection), 0.0), 240.0) * vec3(0.55, 0.68, 0.80) * shadow;
        alpha = mix(0.88, 0.985, fresnel);
    }

    float distanceFromCamera = length(vViewPosition);
    float density = 0.0019;
    if (uMode == 1 || uMode == 3) density += 0.0012 * (1.0 - smoothstep(-8.0, 6.0, vWorld.y));  // mist pools in the basins
    float fog = 1.0 - exp(-distanceFromCamera * density);
    color = mix(color, vec3(0.016, 0.030, 0.045), clamp(fog, 0.0, 0.94));

    // Purkinje shift: scotopic vision drains warm hues from the darkest areas.
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float nightBlend = 1.0 - smoothstep(0.0, 0.14, luminance);
    color = mix(color, luminance * vec3(0.72, 0.86, 1.18), nightBlend * 0.35);

    color = vec3(1.0) - exp(-color * 3.2);
    color = pow(color, vec3(1.0 / 2.2));
    // Gentle filmic S-curve for contrast without crushing the fog bands.
    color = mix(color, color * color * (3.0 - 2.0 * color), 0.22);
    gl_FragColor = vec4(color, alpha);
}

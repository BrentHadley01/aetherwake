#version 120

uniform sampler2D uAlbedo;
uniform sampler2D uRock;
uniform sampler2DShadow uShadow;
uniform mat4 uLight;    // world -> shadow-map texture space
uniform mat4 uInverseView;
uniform int uShadowOn;
uniform int uMode;      // 0 = authored mesh, 1 = streamed terrain, 2 = water, 3 = grass, 4 = sky
uniform float uTime;
uniform vec3 uEye;
// Day/night cycle state, computed on the CPU each frame.
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uAmbient;
uniform vec3 uFog;          // linear-space fog / horizon color
uniform vec3 uZenithLin;    // linear-space zenith color (water reflections)
uniform vec3 uHorizonDisp;  // display-space sky gradient endpoints
uniform vec3 uZenithDisp;
uniform vec3 uCloudDark;
uniform vec3 uCloudLit;
uniform float uNight;       // 1 = full night (drives the Purkinje shift)

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
        // Sky dome: cycle-driven gradient plus drifting lit clouds.
        vec3 direction = normalize(vWorld - uEye);
        float upness = clamp(direction.y * 1.05 + 0.10, 0.0, 1.0);
        vec3 result = mix(uHorizonDisp, uZenithDisp, pow(upness, 0.62));
        if (direction.y > 0.02) {
            vec2 cloudUv = direction.xz / max(direction.y, 0.10) * 0.42 + vec2(uTime * 0.006, uTime * 0.0015);
            float broadCloud = cloudFbm(cloudUv);
            float wisps = cloudFbm(cloudUv * 2.15 + vec2(19.0, -7.0));
            float cloud = smoothstep(0.51, 0.72, broadCloud * 0.74 + wisps * 0.26);
            float horizonFade = smoothstep(0.03, 0.2, direction.y);
            float glow = pow(max(dot(direction, normalize(uLightDir)), 0.0), 16.0);
            vec3 cloudColor = mix(uCloudDark, uCloudLit, wisps);
            cloudColor += glow * uLightColor * 0.35;
            result = mix(result, cloudColor, cloud * horizonFade * 0.68);
            result += glow * uLightColor * 0.06 * (1.0 - cloud);
        }
        gl_FragColor = vec4(result, 1.0);
        return;
    }
    // Rotate the interpolated view-space normal back to world space so the
    // cycle light direction shades consistently regardless of camera yaw.
    vec3 normal = normalize(mat3(uInverseView[0].xyz, uInverseView[1].xyz, uInverseView[2].xyz) * vNormal);
    vec3 lightDirection = normalize(uLightDir);
    vec3 viewDirection = normalize(uEye - vWorld);

    vec3 albedo;
    float specularStrength = 0.02;
    float shininess = 64.0;
    float alpha = 1.0;
    float foliageMask = 0.0;

    if (uMode == 1) {
        // Photoscan tiles cover ~2.4 m; a second, much broader scale breaks
        // the repetition that would otherwise show across clearings.
        vec2 uv = vWorld.xz * 0.42;
        vec3 soilNear = texture2D(uAlbedo, uv).rgb;
        vec3 soilFar = texture2D(uAlbedo, uv * 0.135).rgb;
        vec3 soil = pow(mix(soilNear, soilFar, 0.42), vec3(2.2));   // sRGB -> linear
        // Triplanar projection keeps cliff faces from stretching the rock
        // photoscan vertically. A broad top-down sample breaks repetition.
        vec3 projectionWeight = pow(abs(normal), vec3(4.0));
        projectionWeight /= max(projectionWeight.x + projectionWeight.y + projectionWeight.z, 0.001);
        vec3 rockX = texture2D(uRock, vWorld.zy * 0.18).rgb;
        vec3 rockY = texture2D(uRock, vWorld.xz * 0.18).rgb;
        vec3 rockZ = texture2D(uRock, vWorld.xy * 0.18).rgb;
        vec3 rockDetail = rockX * projectionWeight.x + rockY * projectionWeight.y + rockZ * projectionWeight.z;
        vec3 rock = pow(mix(rockDetail, texture2D(uRock, vWorld.xz * 0.045).rgb, 0.32), vec3(2.2));
        // Micro-relief: treat the albedo as a heightfield and perturb the
        // normal so raking light catches ground detail; fades with distance.
        float detailFade = exp(-length(vViewPosition) * 0.02);
        if (detailFade > 0.02) {
            float lumHere = dot(soilNear, vec3(0.333));
            float lumX = dot(texture2D(uAlbedo, uv + vec2(0.0022, 0.0)).rgb, vec3(0.333));
            float lumZ = dot(texture2D(uAlbedo, uv + vec2(0.0, 0.0022)).rgb, vec3(0.333));
            normal = normalize(normal + vec3(lumHere - lumX, 0.0, lumHere - lumZ) * 2.0 * detailFade);
        }
        float slope = 1.0 - normal.y;
        float rockBlend = smoothstep(0.16, 0.40, slope);
        float patchNoise = valueNoise(vWorld.xz * 0.035 + vec2(8.2, -3.7));
        float fineNoise = valueNoise(vWorld.xz * 0.19 - vec2(11.0, 4.0));
        float moss = smoothstep(0.48, 0.78, patchNoise * 0.72 + fineNoise * 0.28) * (1.0 - rockBlend);
        vec3 forestSoil = soil * vTint.rgb;
        forestSoil = mix(forestSoil, forestSoil * vec3(0.62, 0.92, 0.52), moss * 0.34);
        vec3 ground = mix(forestSoil, rock, rockBlend);
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
        foliageMask = 1.0;
    } else {
        albedo = pow(texture2D(uAlbedo, vUv).rgb, vec3(2.2)) * vTint.rgb;
        // Green-dominant authored materials are foliage; bark, stone, cloth,
        // and skin remain on the opaque surface path.
        foliageMask = smoothstep(0.015, 0.09, albedo.g - max(albedo.r, albedo.b));
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
    vec3 skyBounce = uAmbient * 0.55 * max(normal.y, 0.0);
    vec3 directLight = uLightColor * diffuse;
    vec3 color = albedo * (uAmbient + skyBounce + directLight) + specular * uLightColor;
    // Thin leaves and needles transmit light from behind. This keeps conifer
    // boughs dimensional instead of collapsing into black cut-outs.
    float transmission = max(dot(-normal, lightDirection), 0.0) * foliageMask;
    color += albedo * uLightColor * transmission * (0.22 + 0.16 * max(normal.y, 0.0));

    if (uMode == 2) {
        // Fresnel-weighted reflection of the synthesized night sky, so the
        // lake mirrors the gradient, drifting clouds, and a moon glint.
        vec3 incident = normalize(vWorld - uEye);
        vec3 reflected = reflect(incident, normal);
        reflected.y = max(abs(reflected.y), 0.02);
        float skyT = pow(clamp(reflected.y, 0.0, 1.0), 0.62);
        vec3 skyReflection = mix(uFog * 1.35, uZenithLin, skyT);
        vec2 reflectionUv = reflected.xz / reflected.y * 0.42 + vec2(uTime * 0.006, uTime * 0.0015);
        float cloud = smoothstep(0.51, 0.72, cloudFbm(reflectionUv) * 0.74 + cloudFbm(reflectionUv * 2.15 + vec2(19.0, -7.0)) * 0.26);
        skyReflection = mix(skyReflection, uFog * 0.75, cloud * 0.85);
        float fresnel = 0.15 + 0.85 * pow(1.0 - max(dot(-incident, normal), 0.0), 3.0);
        color = mix(color, skyReflection, fresnel);
        color += pow(max(dot(reflected, lightDirection), 0.0), 240.0) * uLightColor * 0.85 * shadow;
        alpha = mix(0.88, 0.985, fresnel);
    }

    float distanceFromCamera = length(vViewPosition);
    float density = 0.00165;
    float basinHaze = exp(-max(vWorld.y - uEye.y + 9.0, 0.0) * 0.055);
    if (uMode == 1 || uMode == 3) density += 0.00125 * basinHaze;
    float fog = 1.0 - exp(-distanceFromCamera * density);
    vec3 viewRay = normalize(vWorld - uEye);
    float forwardScatter = pow(max(dot(viewRay, lightDirection), 0.0), 8.0);
    vec3 aerialColor = uFog + uLightColor * forwardScatter * 0.10;
    color = mix(color, aerialColor, clamp(fog, 0.0, 0.94));

    // Purkinje shift: scotopic vision drains warm hues from the darkest areas
    // — only after the sun is down.
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float nightBlend = (1.0 - smoothstep(0.0, 0.14, luminance)) * uNight;
    color = mix(color, luminance * vec3(0.72, 0.86, 1.18), nightBlend * 0.35);

    color = vec3(1.0) - exp(-color * 3.2);
    color = pow(color, vec3(1.0 / 2.2));
    // Gentle filmic S-curve for contrast without crushing the fog bands.
    color = mix(color, color * color * (3.0 - 2.0 * color), 0.22);
    gl_FragColor = vec4(color, alpha);
}

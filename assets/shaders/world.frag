#version 120

uniform sampler2D uAlbedo;
uniform sampler2D uRock;
uniform sampler2DShadow uShadow;
uniform mat4 uLight;    // world -> shadow-map texture space
uniform int uShadowOn;
uniform int uMode;      // 0 = authored mesh, 1 = streamed terrain, 2 = water
uniform float uTime;

varying vec3 vNormal;
varying vec3 vViewPosition;
varying vec2 vUv;
varying vec4 vTint;
varying vec3 vWorld;

void main() {
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
        float slope = 1.0 - normal.y;
        float rockBlend = smoothstep(0.16, 0.40, slope);
        vec3 ground = mix(soil * vTint.rgb, rock, rockBlend);
        float high = smoothstep(34.0, 54.0, vWorld.y);       // pale frost-dusted summits
        ground = mix(ground, rock * vec3(1.35, 1.42, 1.52), high * (1.0 - rockBlend * 0.35));
        float wet = smoothstep(-5.6, -7.6, vWorld.y);        // dark saturated shoreline
        albedo = mix(ground, ground * vec3(0.40, 0.39, 0.35), wet);
        specularStrength = 0.03 + 0.25 * wet;
    } else if (uMode == 2) {
        float w1 = sin(vWorld.x * 0.9 + uTime * 1.3) + sin(vWorld.z * 0.7 - uTime * 1.1);
        float w2 = sin((vWorld.x + vWorld.z) * 0.23 + uTime * 0.6);
        float rippleFade = exp(-length(vViewPosition) * 0.012);   // avoid distant moire
        normal = normalize(vec3((w1 * 0.035 + w2 * 0.05) * rippleFade, 1.0, (w1 * 0.04 - w2 * 0.045) * rippleFade));
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
        float rim = pow(1.0 - max(dot(viewDirection, normal), 0.0), 3.0);
        color += rim * vec3(0.05, 0.10, 0.13);
        alpha = mix(alpha, 0.97, rim);
    }

    float distanceFromCamera = length(vViewPosition);
    float density = 0.0019;
    if (uMode == 1 || uMode == 3) density += 0.0012 * (1.0 - smoothstep(-8.0, 6.0, vWorld.y));  // mist pools in the basins
    float fog = 1.0 - exp(-distanceFromCamera * density);
    color = mix(color, vec3(0.016, 0.030, 0.045), clamp(fog, 0.0, 0.94));

    color = vec3(1.0) - exp(-color * 3.2);
    color = pow(color, vec3(1.0 / 2.2));
    gl_FragColor = vec4(color, alpha);
}

#version 120

uniform sampler2D uAlbedo;
varying vec3 vNormal;
varying vec3 vViewPosition;
varying vec2 vUv;
varying vec4 vTint;

void main() {
    vec3 albedo = texture2D(uAlbedo, vUv).rgb * vTint.rgb;
    vec3 normal = normalize(vNormal);
    vec3 lightDirection = normalize(vec3(-0.35, 0.72, 0.48));
    vec3 viewDirection = normalize(-vViewPosition);
    vec3 halfVector = normalize(lightDirection + viewDirection);
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float specular = pow(max(dot(normal, halfVector), 0.0), 42.0) * 0.22;
    vec3 coolAmbient = vec3(0.17, 0.23, 0.27);
    vec3 moonLight = vec3(0.58, 0.78, 0.92) * diffuse;
    vec3 color = albedo * (coolAmbient + moonLight) + specular * vec3(0.65, 0.88, 1.0);
    float distanceFromCamera = length(vViewPosition);
    float fog = 1.0 - exp(-distanceFromCamera * 0.018);
    color = mix(color, vec3(0.025, 0.07, 0.085), clamp(fog, 0.0, 0.72));
    color = vec3(1.0) - exp(-color * 1.55);
    color = pow(color, vec3(1.0 / 2.2));
    gl_FragColor = vec4(color, 1.0);
}

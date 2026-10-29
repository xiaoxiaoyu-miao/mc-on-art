// 自定义后处理 3/3：bloom 合成 + ACES 色调映射 + 调色 + 暗角 + CAS 锐化
#version 330

uniform sampler2D InSampler;      // 场景原图
uniform sampler2D BloomSampler;   // 模糊后的亮部

in vec2 texCoord;

layout(std140) uniform SamplerInfo {
    vec2 OutSize;
    vec2 InSize;
};

layout(std140) uniform EnhanceConfig {
    float BloomIntensity;
    float Exposure;
    float Saturation;
    float Contrast;
    float Vignette;
    float Sharpen;
};

out vec4 fragColor;

vec3 acesFilm(vec3 x) {
    // Narkowicz 2015 近似
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main(){
    vec3 scene = texture(InSampler, texCoord).rgb;
    vec3 bloom = texture(BloomSampler, texCoord).rgb;

    vec3 c = scene + bloom * BloomIntensity;

    // CAS 风格的对比度自适应锐化（用 4 邻域）
    if (Sharpen > 0.0) {
        vec2 texel = 1.0 / InSize;
        vec3 n = texture(InSampler, texCoord + vec2(0.0, -texel.y)).rgb;
        vec3 s = texture(InSampler, texCoord + vec2(0.0,  texel.y)).rgb;
        vec3 w = texture(InSampler, texCoord + vec2(-texel.x, 0.0)).rgb;
        vec3 e = texture(InSampler, texCoord + vec2( texel.x, 0.0)).rgb;
        vec3 blur = (n + s + w + e) * 0.25;
        vec3 sharpened = c + (c - blur) * Sharpen;
        c = mix(c, sharpened, 0.75);
    }

    c *= Exposure;
    c = acesFilm(c);

    // 饱和度
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(luma), c, Saturation);

    // 对比度（围绕 0.5）
    c = clamp((c - 0.5) * Contrast + 0.5, 0.0, 1.0);

    // 暗角
    vec2 uv = texCoord - 0.5;
    float vig = 1.0 - dot(uv, uv) * Vignette;
    c *= mix(1.0, vig, step(0.0, Vignette));

    fragColor = vec4(c, 1.0);
}

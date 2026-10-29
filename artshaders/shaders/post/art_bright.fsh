// 自定义后处理 1/3：亮部提取（bloom 的输入）
#version 330

uniform sampler2D InSampler;

in vec2 texCoord;

layout(std140) uniform SamplerInfo {
    vec2 OutSize;
    vec2 InSize;
};

layout(std140) uniform BrightConfig {
    float Threshold;
    float Knee;
};

out vec4 fragColor;

void main(){
    vec3 c = texture(InSampler, texCoord).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    // 软膝：阈值附近平滑过渡，避免硬边
    float soft = clamp(lum - Threshold + Knee, 0.0, 2.0 * Knee);
    soft = soft * soft / (4.0 * Knee + 1e-4);
    float contrib = max(soft, lum - Threshold) / max(lum, 1e-4);
    fragColor = vec4(c * contrib, 1.0);
}

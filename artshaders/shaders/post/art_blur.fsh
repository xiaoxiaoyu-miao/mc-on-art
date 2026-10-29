// 自定义后处理 2/3：可分离高斯模糊（H/V 用 Direction 切换）
#version 330

uniform sampler2D InSampler;

in vec2 texCoord;

layout(std140) uniform SamplerInfo {
    vec2 OutSize;
    vec2 InSize;
};

layout(std140) uniform BlurConfig {
    vec2 Direction;
    float Radius;
};

out vec4 fragColor;

void main(){
    vec2 texel = 1.0 / InSize;
    vec2 d = Direction * texel * Radius;

    vec3 sum = texture(InSampler, texCoord).rgb * 0.227027;
    sum += texture(InSampler, texCoord + d * 1.3846).rgb * 0.316216;
    sum += texture(InSampler, texCoord - d * 1.3846).rgb * 0.316216;
    sum += texture(InSampler, texCoord + d * 3.2308).rgb * 0.070270;
    sum += texture(InSampler, texCoord - d * 3.2308).rgb * 0.070270;
    fragColor = vec4(sum, 1.0);
}

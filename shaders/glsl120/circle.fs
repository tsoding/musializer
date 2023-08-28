#version 120

// Input vertex attributes (from vertex shader)
varying vec2 fragTexCoord;
varying vec4 fragColor;

uniform float radius;
uniform float power;

void main()
{
    float r = radius;
    vec2 p = fragTexCoord - vec2(0.5);
    if (length(p) <= 0.5) {
        float s = length(p) - r;
        if (s <= 0) {
            gl_FragColor = fragColor*1.5;
        } else {
            float t = 1 - s / (0.5 - r);
            gl_FragColor = mix(vec4(fragColor.xyz, 0), fragColor*1.5, pow(t, power));
        }
    } else {
        gl_FragColor = vec4(0);
    }
}

#version 330

// Day/night crossfade for sky and mid parallax layers.
//
// One full-screen-width quad is drawn per layer; this shader tiles the texture
// horizontally via mod() and crossfades day/night in linear color space with a
// dusk-tint kick at nightAmount ~= 0.5.
//
// fragTexCoord.x: 0..1 across the destination (which spans the full screen width)
// fragTexCoord.y: 0..1 across the destination (which is exactly one tile tall)

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D dayTex;          // texture0 — bound by raylib for the active draw
uniform sampler2D nightTex;        // texture1 — bound manually via SetShaderValueTexture
uniform float nightAmount;         // 0 = noon, 1 = midnight
uniform vec3 duskTint;             // multiplied in during the dusk/dawn peak
uniform vec2 dayOffset;            // (px, 0) scroll offset for day layer
uniform vec2 nightOffset;          // same for night
uniform vec2 layerSize;            // (textureWidth, textureHeight) in pixels
uniform vec2 screenSize;           // virtual screen width × tile height (NOT VIRTUAL_H)

out vec4 finalColor;

vec4 sampleWrapped(sampler2D tex, float screenPxX, vec2 offset)
{
    // Continuous (sub-pixel) sampling — the bound day/night textures are
    // bilinear-filtered, so this scrolls smoothly to match the bilinear pipes +
    // ground. (Don't floor to the texel grid here: that would re-snap the
    // motion and bring back the scroll judder.)
    float wrappedX = mod(screenPxX + offset.x, layerSize.x);
    if (wrappedX < 0.0) wrappedX += layerSize.x;
    vec2 uv = vec2(wrappedX / layerSize.x, fragTexCoord.y);
    return texture(tex, uv);
}

void main()
{
    float screenPxX = fragTexCoord.x * screenSize.x;
    vec4 day = sampleWrapped(dayTex, screenPxX, dayOffset);
    vec4 night = sampleWrapped(nightTex, screenPxX, nightOffset);

    // Mix in linear (squared) color space — sRGB linear-blend is noticeably less
    // muddy mid-transition.
    vec3 dayLin = day.rgb * day.rgb;
    vec3 nightLin = night.rgb * night.rgb;
    vec3 mixed = mix(dayLin, nightLin, nightAmount);

    // Dusk tint peaks at nightAmount = 0.5; warmth comes from theme.duskTint.
    float duskWeight = 4.0 * nightAmount * (1.0 - nightAmount);
    mixed = mix(mixed, mixed * duskTint, duskWeight * 0.4);

    // Back to display gamma.
    mixed = sqrt(max(mixed, vec3(0.0)));

    float alpha = mix(day.a, night.a, nightAmount);
    finalColor = vec4(mixed, alpha) * fragColor;
}

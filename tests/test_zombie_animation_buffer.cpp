#include "zombie_animation_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }

// Independent IEEE binary16 decoder for checking the packed upload bytes.
float decoded(std::uint16_t bits) {
    const float sign=(bits&0x8000u) ? -1.f : 1.f;
    const int exponent=(bits>>10)&31;
    const auto fraction=bits&1023u;
    if (!exponent) return sign*std::ldexp(float(fraction),-24);
    if (exponent==31) return fraction ? std::numeric_limits<float>::quiet_NaN() :
                                      sign*std::numeric_limits<float>::infinity();
    return sign*std::ldexp(float(1024+fraction),exponent-25);
}

std::uint32_t floatBits(float value) {
    std::uint32_t bits=0;
    std::memcpy(&bits,&value,sizeof(bits));
    return bits;
}
void unchanged(const std::vector<viewer::AnimationVertex>& actual,
               const std::vector<viewer::AnimationVertex>& expected) {
    require(actual.size()==expected.size(),"Packing changed the source sample count");
    for (std::size_t sample=0;sample<actual.size();++sample) for (int axis=0;axis<4;++axis)
        require(floatBits(actual[sample].position[axis])==floatBits(expected[sample].position[axis]) &&
                floatBits(actual[sample].normal[axis])==floatBits(expected[sample].normal[axis]),
                "GPU packing must preserve every source float32 bit");
}

void knownEncodingsAndLayout() {
    struct Golden { float value;std::uint16_t bits;float result; };
    const auto midpoint=1.f+std::ldexp(1.f,-11);
    // The pinned GLM encoder rounds exact midpoints away from zero.
    for (const Golden sample:{Golden{0,0,0}, {-0.f,0x8000u,-0.f}, {1,0x3c00u,1},
                              {-2,0xc000u,-2}, {.5f,0x3800u,.5f}, {65504,0x7bffu,65504},
                              {-65504,0xfbffu,-65504}, {std::ldexp(1.f,-14),0x0400u,std::ldexp(1.f,-14)},
                              {std::ldexp(1.f,-24),0x0001u,std::ldexp(1.f,-24)},
                              {std::ldexp(1.f,-25),0x0001u,std::ldexp(1.f,-24)},
                              {std::ldexp(1.f,-26),0,0}, {midpoint,0x3c01u,1.f+std::ldexp(1.f,-10)},
                              {-midpoint,0xbc01u,-1.f-std::ldexp(1.f,-10)},
                              {std::nextafter(midpoint,1.f),0x3c00u,1},
                              {std::nextafter(midpoint,2.f),0x3c01u,1.f+std::ldexp(1.f,-10)}}) {
        const std::vector<viewer::AnimationVertex> source{{{sample.value,2,-3,1},{0,1,0,0}}};
        const auto before=source;
        const auto packed=viewer::packZombieAnimation(source);
        require(packed && packed->size()==1,"Finite representable samples must use half storage");
        const auto& bytes=packed->front();
        require(bytes[0]==sample.bits && floatBits(decoded(bytes[0]))==floatBits(sample.result),
                "Binary16 encoding differs from the independent numerical fixture");
        require(sizeof(bytes)==16 && bytes.size()==8 && bytes[1]==0x4000u && bytes[2]==0xc200u &&
                bytes[3]==0x3c00u && bytes[4]==0 && bytes[5]==0x3c00u && bytes[6]==0 && bytes[7]==0,
                "Two RGBA16F texels must pack position xyzw followed by normal xyzw in sixteen bytes");
        unchanged(source,before);
    }
    const auto empty=viewer::packZombieAnimation({});
    require(empty && empty->empty(),"Empty input must retain an empty packed result");
}

void boundedErrorAndFallback() {
    const std::vector<viewer::AnimationVertex> source{
        {{-.987654f,.0000001f,1.7999f,1},{.5773503f,-.7071068f,.4082483f,0}},
        {{1.12345f,-1.65432f,-.3333333f,1},{-.8f,.6f,0,0}}};
    const auto before=source;
    const auto packed=viewer::packZombieAnimation(source);
    require(packed && packed->size()==source.size(),"Ordinary meter-space positions must pack");
    for (std::size_t sample=0;sample<source.size();++sample) for (int axis=0;axis<3;++axis) {
        require(std::abs(decoded((*packed)[sample][axis])-source[sample].position[axis])<.001f,
                "Ordinary animated positions must stay within one millimeter after packing");
        require(std::abs(decoded((*packed)[sample][4+axis])-source[sample].normal[axis])<.0005f,
                "Packed normal components exceeded the independent error budget");
    }
    unchanged(source,before);
    for (int component=0;component<8;++component) for (const auto invalid:{65505.f,-70000.f,
            std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        auto bad=source;
        (component<4 ? bad.back().position[component] : bad.back().normal[component-4])=invalid;
        const auto original=bad;
        require(!viewer::packZombieAnimation(bad),"Unsafe component must preserve the float32 upload fallback");
        unchanged(bad,original);
    }
    auto tiny=source;
    tiny[0].normal={std::ldexp(1.f,-26),-std::ldexp(1.f,-26),0,0};
    require(!viewer::packZombieAnimation(tiny),"A nonzero normal must not collapse to a zero vector");
    tiny[0].normal={-0.f,0,0,0};
    const auto zero=viewer::packZombieAnimation(tiny);
    require(zero && zero->front()[4]==0x8000u && zero->front()[5]==0 && zero->front()[6]==0,
            "Originally zero normals, including signed zeros, must remain representable");
}

void boundsEncloseStoredEndpointsAndInterpolation() {
    const std::vector<viewer::AnimationVertex> source{
        {{-32.02f,-1.0006f,-.3334f,1},{0,0,1,0}}, {{32.02f,.3334f,1.0006f,1},{0,0,1,0}},
        {{-17.009f,2.001f,.00000003f,1},{0,0,1,0}}, {{31.99f,-2.001f,-1.0006f,1},{0,0,1,0}},
        {{-32.01f,.1f,0,1},{0,0,1,0}}, {{31.98f,.2f,.3f,1},{0,0,1,0}}};
    const auto before=source;
    viewer::VisibilityBounds original{glm::vec3(std::numeric_limits<float>::max()),
                                      glm::vec3(std::numeric_limits<float>::lowest())};
    for (const auto& sample:source) {
        original.minimum=glm::min(original.minimum,glm::vec3(sample.position));
        original.maximum=glm::max(original.maximum,glm::vec3(sample.position));
    }
    const auto bounds=viewer::zombieAnimationHalfBounds(original);
    require(bounds.minimum.x<=-32.03125f && bounds.maximum.x>=32.03125f,
            "Rounded endpoints extending outside the original box must enlarge it");
    require(glm::all(glm::lessThanEqual(bounds.minimum,original.minimum)) &&
            glm::all(glm::greaterThanEqual(bounds.maximum,original.maximum)),
            "Half bounds must retain every original float32 endpoint");
    const auto packed=viewer::packZombieAnimation(source);
    require(packed.has_value(),"Bounds fixture must use half storage");
    for (std::size_t frame=0;frame<3;++frame) for (std::size_t vertex=0;vertex<2;++vertex)
        for (const float fraction:{0.f,.125f,.25f,.5f,.75f,.875f,1.f}) for (int axis=0;axis<3;++axis) {
            const float first=decoded((*packed)[frame*2+vertex][axis]);
            const float second=decoded((*packed)[((frame+1)%3)*2+vertex][axis]);
            const float value=first*(1-fraction)+second*fraction;
            require(value>=bounds.minimum[axis] && value<=bounds.maximum[axis],
                    "Bounds must enclose stored frame endpoints and interpolated wraparound poses");
        }
    unchanged(source,before);
    const viewer::VisibilityBounds overflow{{-70000,-1,-1},{70000,1,1}};
    const auto safe=viewer::zombieAnimationHalfBounds(overflow);
    require(safe.minimum.x==-70000 && safe.maximum.x==70000,"Float32 fallback bounds must retain large finite coordinates");
    const viewer::VisibilityBounds empty{glm::vec3(std::numeric_limits<float>::max()),
                                        glm::vec3(std::numeric_limits<float>::lowest())};
    const auto stillEmpty=viewer::zombieAnimationHalfBounds(empty);
    require(stillEmpty.minimum==empty.minimum && stillEmpty.maximum==empty.maximum,
            "Empty-bounds sentinels must survive half-bound preparation");
}
} // namespace

int main() {
    try {
        knownEncodingsAndLayout();
        boundedErrorAndFallback();
        boundsEncloseStoredEndpointsAndInterpolation();
        std::cout << "Zombie animation buffer tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n';return 1; }
}

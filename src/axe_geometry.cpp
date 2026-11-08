#include "axe_geometry.hpp"
#include "melee_weapon_geometry.hpp"

namespace viewer {
Model makeAxeModel() {
    Model model; model.materials.emplace_back();
    meleeWeaponPrism(model, std::array<glm::vec2, 4>{{{-.018f, -.20f}, {.018f, -.20f},
        {.018f, axeHeadBackTop}, {-.018f, axeHeadBackTop}}}, .018f, {.27f, .13f, .055f});
    // The head flares from its narrow socket toward the full-height edge.
    meleeWeaponPrism(model, std::array<glm::vec2, 4>{{{axeHeadBack, axeHeadBackBottom}, {axeHeadFront, axeHeadBottom},
        {axeHeadFront, axeHeadTop}, {axeHeadBack, axeHeadBackTop}}}, axeHeadHalfDepth, {.28f, .31f, .34f});
    // A bright tapered wedge joins the solid head to the separate cutting edge.
    Primitive wedge; wedge.material = 0;
    const auto triangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        const auto normal = glm::normalize(glm::cross(c - a, b - a));
        for (const auto point : {a, c, b}) {
            Vertex v; v.position = point; v.normal = normal; v.color = {.72f, .77f, .81f, 1};
            wedge.indices.push_back(static_cast<std::uint32_t>(wedge.vertices.size()));
            wedge.vertices.push_back(v);
        }
    };
    const glm::vec3 a(axeHeadFront, axeHeadBottom, -axeHeadHalfDepth), b(axeHeadFront, axeHeadBottom, axeHeadHalfDepth),
        c(axeEdgeX, axeHeadBottom, 0), d(axeHeadFront, axeHeadTop, -axeHeadHalfDepth),
        e(axeHeadFront, axeHeadTop, axeHeadHalfDepth), f(axeEdgeX, axeHeadTop, 0);
    triangle(a, b, c); triangle(d, f, e);
    triangle(a, d, e); triangle(a, e, b);
    triangle(b, e, f); triangle(b, f, c);
    triangle(c, f, d); triangle(c, d, a);
    DrawInstance draw; draw.primitive = model.primitives.size(); model.draws.push_back(draw);
    model.primitives.push_back(std::move(wedge));
    model.boundsMin = {axeHeadBack, -.20f, -axeHeadHalfDepth};
    model.boundsMax = {axeEdgeX, axeHeadTop, axeHeadHalfDepth};
    return model;
}
}

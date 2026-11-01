#include "display/render/quad.h"
#include "display/render/quadarray.h"
#include "display/render/tilequad.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

static std::vector<gpu::QuadVertex> submitted;
static unsigned submissions = 0;

namespace gpu {
void drawVertices(std::span<const QuadVertex> vertices) {
    submitted.assign(vertices.begin(), vertices.end());
    ++submissions;
}
}

static void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Function>
static void rejects(Function function) {
    bool rejected = false;
    try { function(); }
    catch (const Exception &error) { rejected = error.type == Exception::ArgumentError; }
    check(rejected, "invalid geometry must raise the engine ArgumentError");
}

static void testQuad() {
    Quad quad;
    quad.setPosRect(FloatRect(10, 20, -4, 6));
    quad.setTexRect(FloatRect(2, 3, 8, -2));
    quad.setColor(Vec4(.25f, .5f, .75f, 1));
    quad.draw();
    check(!quad.vboDirty && submitted.size() == 6, "quad dirty geometry committed");
    constexpr float positions[][2] = {{10,20}, {6,20}, {6,26}, {6,26}, {10,26}, {10,20}};
    for (size_t i = 0; i < 6; ++i) {
        check(submitted[i].position[0] == positions[i][0] &&
              submitted[i].position[1] == positions[i][1], "quad winding and flipped positions");
        check(submitted[i].color[0] == .25f && submitted[i].color[2] == .75f, "vertex tint");
    }
    check(submitted[2].texcoord[0] == 10 && submitted[2].texcoord[1] == 1, "flipped UV corner");
    quad.vert[0].pos.x = 90;
    quad.draw();
    check(submitted[0].position[0] == 10, "direct edits retain committed quad until update");
    quad.updateBuffer();
    quad.draw();
    check(submitted[0].position[0] == 90, "explicit quad update refreshes snapshot");
}

static void testBatch() {
    SimpleQuadArray batch;
    batch.resize(2);
    Quad::setTexPosRect(batch.vertices.data(), FloatRect(0,0,4,8), FloatRect(1,2,4,8));
    Quad::setTexPosRect(batch.vertices.data()+4, FloatRect(5,6,2,3), FloatRect(20,30,2,3));
    rejects([&] { batch.draw(); });
    batch.commit();
    batch.vertices[4].pos.x = 100;
    const auto calls = submissions;
    batch.draw();
    check(submissions == calls + 1 && submitted.size() == 12, "one batched triangle submission");
    batch.draw(1, 1);
    check(submitted.size() == 6 && submitted[0].position[0] == 20, "committed subrange snapshot");
    check(submitted[0].color[0] == 1 && submitted[0].color[3] == 1, "simple vertices default white");
    batch.commit();
    batch.draw(1, 1);
    check(submitted[0].position[0] == 100, "recommit applies direct vertex edits");
    rejects([&] { batch.draw(2, 1); });
    rejects([&] { batch.draw(std::numeric_limits<size_t>::max(), 1); });
    rejects([&] { batch.resize(std::numeric_limits<size_t>::max()); });
    batch.clear();
    const auto before = submissions;
    batch.draw();
    check(batch.count() == 0 && submissions == before, "empty batch submits no geometry");

    SimpleQuadArray largest;
    constexpr size_t limit = static_cast<size_t>(INDEX_T_MAX - 1) / 6;
    largest.resize(limit);
    Quad::setPosRect(largest.vertices.data() + (limit - 1) * 4, FloatRect(7,8,9,10));
    largest.commit();
    largest.draw(limit - 1, 1);
    check(submitted[2].position[0] == 16 && submitted[2].position[1] == 18,
          "last legal indexed quad retains all geometry");
    rejects([&] { largest.resize(limit + 1); });
    check(largest.count() == limit, "rejected index range leaves existing batch intact");

    CVertex colored;
    colored.pos = Vec2(3,4);
    colored.color = Vec4(1,.5f,.25f,0);
    const auto vertex = VertexTraits<CVertex>::convert(colored);
    check(vertex.texcoord[0] == 0 && vertex.texcoord[1] == 0 && vertex.color[3] == 0,
          "untextured color vertex preserves transparency and defaults UV");
}

static void testTiling() {
    std::array<Vertex, 36> vertices;
    const int count = TileQuads::build(IntRect(3,4,5,7), IntRect(10,20,12,15), vertices.data());
    check(count == 9, "partial row and column tile counts");
    const auto &last = vertices[8*4+2];
    check(last.pos == Vec2(22,35) && last.texPos == Vec2(5,5), "partial tile position and UV edges");
    check(TileQuads::oneDimCount(0, 20) == 0 && TileQuads::twoDimCount(5,7,12,15) == 9,
          "tile count boundaries");
}

int main() {
    try {
        testQuad();
        testBatch();
        testTiling();
        std::cout << "CPU quad geometry tests passed\n";
        return 0;
    } catch (const Exception &error) {
        std::cerr << error.msg << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
    }
    return 1;
}

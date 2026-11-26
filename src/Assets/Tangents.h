#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

struct Vertex;

bool generateTangents(std::vector<Vertex>& vertices, size_t base, std::span<uint32_t> indices,
                      float authoredHandednessScale = 0.0f);

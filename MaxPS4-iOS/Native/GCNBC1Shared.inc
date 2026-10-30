// Intentionally unguarded: included as C++ and as Metal source text.
GCN_BC1_SOURCE(
inline float gcn_bc1_component(unsigned endpoints, unsigned selectors, unsigned pixel, unsigned channel) {
    unsigned a = endpoints & 65535u, b = endpoints >> 16;
    unsigned index = (selectors >> (2u * pixel)) & 3u;
    bool transparent = a <= b && index == 3u;
    if (channel == 3u) return transparent ? 0.0f : 1.0f;
    if (transparent) return 0.0f;
    unsigned shift = channel == 0u ? 11u : channel == 1u ? 5u : 0u;
    unsigned mask = channel == 1u ? 63u : 31u;
    float c0 = float((a >> shift) & mask) / float(mask);
    float c1 = float((b >> shift) & mask) / float(mask);
    if (index == 0u) return c0;
    if (index == 1u) return c1;
    if (a <= b) return (c0 + c1) * 0.5f;
    return index == 2u ? (2.0f * c0 + c1) / 3.0f : (c0 + 2.0f * c1) / 3.0f;
}
)

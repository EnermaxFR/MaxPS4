#import "../Native/PM4MetalDrawBridge.h"
#include <cassert>
#include <cstdio>

// Records actual Objective-C calls without claiming GPU execution.
@interface RecordingEncoder : NSObject
@property NSUInteger pipelines;
@property NSUInteger draws;
- (void)setRenderPipelineState:(id<MTLRenderPipelineState>)pipeline;
- (void)drawPrimitives:(MTLPrimitiveType)type vertexStart:(NSUInteger)start vertexCount:(NSUInteger)count;
@end
@implementation RecordingEncoder
- (void)setRenderPipelineState:(id<MTLRenderPipelineState>)pipeline {
    assert(pipeline); ++_pipelines;
}
- (void)drawPrimitives:(MTLPrimitiveType)type vertexStart:(NSUInteger)start vertexCount:(NSUInteger)count {
    assert(type == MTLPrimitiveTypeTriangle && start == 0 && count == 3); ++_draws;
}
@end
int main() {
    @autoreleasepool {
        RecordingEncoder* recorder = [RecordingEncoder new];
        id<MTLRenderCommandEncoder> encoder = (id<MTLRenderCommandEncoder>)recorder;
        id<MTLRenderPipelineState> pipeline = (id<MTLRenderPipelineState>)[NSObject new];
        uint32_t words[] = {0xc0012d00, 3, 2, 0xc0012d00, 3, 2};
        size_t drawn = 99;
        assert(maxps4_pm4_encode_triangle(words, 6, encoder, pipeline, &drawn) == 0);
        assert(drawn == 2 && recorder.draws == 2 && recorder.pipelines == 1);
        words[5] = 0;
        assert(maxps4_pm4_encode_triangle(words, 6, encoder, pipeline, &drawn) != 0);
        assert(drawn == 0 && recorder.draws == 2 && recorder.pipelines == 1);
        const uint32_t state[] = {0xc0016900, 0, 1, 0xc0012d00, 3, 2};
        assert(maxps4_pm4_encode_triangle(state, 6, encoder, pipeline, &drawn) != 0);
        assert(recorder.draws == 2 && recorder.pipelines == 1);
        assert(maxps4_pm4_encode_triangle(words, 3, nil, pipeline, &drawn) != 0);
        assert(maxps4_pm4_encode_triangle(words, 3, encoder, nil, &drawn) != 0);
        assert(maxps4_pm4_encode_triangle(words, 3, encoder, pipeline, nullptr) != 0);
        assert(maxps4_pm4_encode_triangle(nullptr, 1, encoder, pipeline, &drawn) != 0);
        assert(recorder.draws == 2 && recorder.pipelines == 1);
        std::puts("PASS exported PM4 ABI: draw calls recorded; failures do not touch encoder");
    }
}

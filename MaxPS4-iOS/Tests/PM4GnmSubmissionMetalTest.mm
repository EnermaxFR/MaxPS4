#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "../Native/maxps4_ps4_gnm_queue.h"
#include "../Native/maxps4_ps4_gnm.h"
#import "../Native/PM4MetalDrawBridge.h"
#include "GuestELFFixture.h"
#include <cassert>
#include <cstdio>

int main() {
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        if(!device) { std::puts("SKIP GNM submission Metal readback: no Metal device"); return 0; }
        maxps4::GuestELF image; auto file=guest_test::elf(40,2,true);
        guest_test::put(file,4096,160,8); assert(image.load(file.data(),file.size(),6)==0);
        maxps4::PS4GuestMemory memory(image);
        maxps4::PS4Gnm writer(memory); maxps4::PS4GnmQueue submissions(memory);
        constexpr uint64_t command=0x10000, addresses=command+0x100, sizes=command+0x180;
        uint8_t table[8]{}; maxps4::ps4::put_le(table,0,command,8); assert(memory.write(addresses,table,8));
        maxps4::ps4::put_le(table,0,28,4); assert(memory.write(sizes,table,4));
        assert(writer.draw_auto(command,7,3,0)==0);
        assert(submissions.backend(true)==0 && submissions.submit(1,addresses,sizes,0,0)==0);
        assert(submissions.done()==0 && !submissions.allowed());
        maxps4_gnm_submission job{}; uint32_t words[7]{};
        assert(submissions.snapshot(job,words,7)==0 && job.draw_count==1);
        // Mutating guest commands after submission cannot alter the owned job.
        maxps4::ps4::put_le(table,0,0xffffffff,4); assert(memory.write(command,table,4));
        assert(words[0]==0xc0012d00);
        NSString* shader=@"#include <metal_stdlib>\nusing namespace metal;\n"
            "vertex float4 vertexMain(uint id [[vertex_id]]) { "
            "float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)}; return float4(p[id],0,1); }\n"
            "fragment float4 fragmentMain() { return float4(1,0,0,1); }\n";
        NSError* error=nil;
        id<MTLLibrary> library=[device newLibraryWithSource:shader options:nil error:&error]; assert(library && !error);
        MTLRenderPipelineDescriptor* pipelineDescriptor=[MTLRenderPipelineDescriptor new];
        pipelineDescriptor.vertexFunction=[library newFunctionWithName:@"vertexMain"];
        pipelineDescriptor.fragmentFunction=[library newFunctionWithName:@"fragmentMain"];
        pipelineDescriptor.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
        id<MTLRenderPipelineState> pipeline=[device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error]; assert(pipeline && !error);
        MTLTextureDescriptor* descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:8 height:8 mipmapped:NO];
        descriptor.storageMode=MTLStorageModeShared; descriptor.usage=MTLTextureUsageRenderTarget;
        id<MTLTexture> texture=[device newTextureWithDescriptor:descriptor]; assert(texture);
        MTLRenderPassDescriptor* pass=[MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture=texture; pass.colorAttachments[0].loadAction=MTLLoadActionClear;
        pass.colorAttachments[0].storeAction=MTLStoreActionStore; pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);
        id<MTLCommandQueue> queue=[device newCommandQueue]; assert(queue);
        id<MTLCommandBuffer> buffer=[queue commandBuffer]; assert(buffer);
        id<MTLRenderCommandEncoder> encoder=[buffer renderCommandEncoderWithDescriptor:pass]; assert(encoder);
        size_t drawn=0; assert(maxps4_pm4_encode_triangle(words,7,encoder,pipeline,&drawn)==0 && drawn==1);
        [encoder endEncoding]; [buffer commit]; [buffer waitUntilCompleted];
        assert(buffer.status==MTLCommandBufferStatusCompleted && !buffer.error);
        assert(!submissions.allowed()); // GPU completion alone has not acknowledged the ticket.
        uint8_t pixels[8*8*4]{};
        [texture getBytes:pixels bytesPerRow:8*4 fromRegion:MTLRegionMake2D(0,0,8,8) mipmapLevel:0];
        for(size_t i=0;i<64;++i) assert(pixels[i*4]==255 && pixels[i*4+1]==0 && pixels[i*4+2]==0 && pixels[i*4+3]==255);
        assert(submissions.complete(job.ticket,true)==0 && submissions.allowed()==1);
        assert(submissions.complete(job.ticket,true)==-EINVAL);
        std::puts("PASS GNM packet writer -> owned submission queue -> real Metal GPU completion -> explicit acknowledgment and pixel readback; native diagnostic shaders only");
    }
}

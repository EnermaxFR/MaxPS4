#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __OBJC__
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
NS_ASSUME_NONNULL_BEGIN
// Reader is invoked only synchronously during submit, while the guest's
// submitting thread is paused. Neither reader nor submit may reenter GNM.
typedef BOOL (^MaxPS4GameMemoryReader)(uint64_t, void*, size_t);
@interface MaxPS4GameMetalBridge : NSObject
- (nullable instancetype)initWithDevice:(id<MTLDevice>)device
                                reader:(MaxPS4GameMemoryReader)reader;
- (int32_t)submit:(const uint32_t*)words count:(size_t)count;
// Returns an owned, immutable display copy, cropped to VideoOut dimensions.
// nil while a submission holds the renderer, or if the address was not drawn.
- (nullable id<MTLTexture>)copyTargetAtAddress:(uint64_t)address
                                       width:(uint32_t)width height:(uint32_t)height
    NS_SWIFT_NAME(copyTarget(address:width:height:));
@property(nonatomic, readonly, copy) NSString* report;
@property(nonatomic, readonly, copy, nullable) NSString* failure;
@end
NS_ASSUME_NONNULL_END
#endif

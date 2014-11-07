// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from rocketspeed.djinni

#import "RSNamespaceID+Private.h"
#import <Foundation/Foundation.h>
#include <utility>

static_assert(__has_feature(objc_arc), "Djinni requires ARC to be enabled for this file");

@implementation RSNamespaceID

- (id)initWithNamespaceID:(RSNamespaceID *)NamespaceID
{
    if (self = [super init]) {
        _nsid = NamespaceID.nsid;
    }
    return self;
}

- (id)initWithNsid:(int32_t)nsid
{
    if (self = [super init]) {
        _nsid = nsid;
    }
    return self;
}

- (id)initWithCppNamespaceID:(const ::rocketglue::NamespaceID &)NamespaceID
{
    if (self = [super init]) {
        _nsid = NamespaceID.nsid;
    }
    return self;
}

- (::rocketglue::NamespaceID)cppNamespaceID
{
    int32_t nsid = _nsid;
    return ::rocketglue::NamespaceID(
            std::move(nsid));
}

@end
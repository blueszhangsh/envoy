#include "source/common/tracing/custom_tag_impl.h"

#include "envoy/router/router.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Tracing {

void CustomTagBase::applySpan(Span& span, const CustomTagContext& ctx) const {
  absl::string_view tag_value = value(ctx);
  if (!tag_value.empty()) {
    span.setTag(tag(), tag_value);
  }
}

EnvironmentCustomTag::EnvironmentCustomTag(
    const std::string& tag, const envoy::type::tracing::v3::CustomTag::Environment& environment)
    : CustomTagBase(tag), name_(environment.name()), default_value_(environment.default_value()) {
  const char* env = std::getenv(name_.data());
  final_value_ = env ? env : default_value_;
}

RequestHeaderCustomTag::RequestHeaderCustomTag(
    const std::string& tag, const envoy::type::tracing::v3::CustomTag::Header& request_header)
    : CustomTagBase(tag), name_(Http::LowerCaseString(request_header.name())),
      default_value_(request_header.default_value()) {}

absl::string_view RequestHeaderCustomTag::value(const CustomTagContext& ctx) const {
  if (ctx.trace_context == nullptr) {
    return default_value_;
  }
  // TODO(https://github.com/envoyproxy/envoy/issues/13454): Potentially populate all header values.
  const auto entry = ctx.trace_context->getByKey(name_);
  return entry.value_or(default_value_);
}

MetadataCustomTag::MetadataCustomTag(const std::string& tag,
                                     const envoy::type::tracing::v3::CustomTag::Metadata& metadata)
    : CustomTagBase(tag), kind_(metadata.kind().kind_case()),
      metadata_key_(metadata.metadata_key()), default_value_(metadata.default_value()) {}

void MetadataCustomTag::applySpan(Span& span, const CustomTagContext& ctx) const {
  const envoy::config::core::v3::Metadata* meta = metadata(ctx);
  const auto meta_str = metadataToString(meta);

  if (!meta_str.has_value()) {
    if (!default_value_.empty()) {
      span.setTag(tag(), default_value_);
    }
    return;
  }

  span.setTag(tag(), meta_str.value());
}

absl::optional<std::string>
MetadataCustomTag::metadataToString(const envoy::config::core::v3::Metadata* metadata) const {
  if (!metadata) {
    return absl::nullopt;
  }

  const ProtobufWkt::Value& value = Envoy::Config::Metadata::metadataValue(metadata, metadata_key_);
  switch (value.kind_case()) {
  case ProtobufWkt::Value::kBoolValue:
    return value.bool_value() ? "true" : "false";
  case ProtobufWkt::Value::kNumberValue:
    return absl::StrCat(value.number_value());
  case ProtobufWkt::Value::kStringValue:
    return value.string_value();
  case ProtobufWkt::Value::kListValue:
    return MessageUtil::getJsonStringFromMessageOrDie(value.list_value());
  case ProtobufWkt::Value::kStructValue:
    return MessageUtil::getJsonStringFromMessageOrDie(value.struct_value());
  default:
    break;
  }

  return absl::nullopt;
}

const envoy::config::core::v3::Metadata*
MetadataCustomTag::metadata(const CustomTagContext& ctx) const {
  const StreamInfo::StreamInfo& stream_info = ctx.stream_info;
  switch (kind_) {
  case envoy::type::metadata::v3::MetadataKind::KindCase::kRequest:
    return &stream_info.dynamicMetadata();
  case envoy::type::metadata::v3::MetadataKind::KindCase::kRoute: {
    Router::RouteConstSharedPtr route = stream_info.route();
    return route ? &route->metadata() : nullptr;
  }
  case envoy::type::metadata::v3::MetadataKind::KindCase::kCluster: {
    if (stream_info.upstreamInfo().has_value() &&
        stream_info.upstreamInfo().value().get().upstreamHost()) {
      return &stream_info.upstreamInfo().value().get().upstreamHost()->cluster().metadata();
    }
    return nullptr;
  }
  case envoy::type::metadata::v3::MetadataKind::KindCase::kHost: {
    if (stream_info.upstreamInfo().has_value() &&
        stream_info.upstreamInfo().value().get().upstreamHost()) {
      return stream_info.upstreamInfo().value().get().upstreamHost()->metadata().get();
    }
    return nullptr;
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

CustomTagConstSharedPtr
CustomTagUtility::createCustomTag(const envoy::type::tracing::v3::CustomTag& tag) {
  switch (tag.type_case()) {
  case envoy::type::tracing::v3::CustomTag::TypeCase::kLiteral:
    return std::make_shared<const Tracing::LiteralCustomTag>(tag.tag(), tag.literal());
  case envoy::type::tracing::v3::CustomTag::TypeCase::kEnvironment:
    return std::make_shared<const Tracing::EnvironmentCustomTag>(tag.tag(), tag.environment());
  case envoy::type::tracing::v3::CustomTag::TypeCase::kRequestHeader:
    return std::make_shared<const Tracing::RequestHeaderCustomTag>(tag.tag(), tag.request_header());
  case envoy::type::tracing::v3::CustomTag::TypeCase::kMetadata:
    return std::make_shared<const Tracing::MetadataCustomTag>(tag.tag(), tag.metadata());
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Tracing
} // namespace Envoy

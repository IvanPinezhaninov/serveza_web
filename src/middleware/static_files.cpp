/******************************************************************************
**
** Copyright (C) 2026 Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>
**
** This file is part of serveza_web, which can be found at
** https://github.com/IvanPinezhaninov/serveza_web/.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
** DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
** OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
** OR OTHER DEALINGS IN THE SOFTWARE.
**
******************************************************************************/

#include <serveza/web/middleware/static_files.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <boost/beast/http.hpp>

#include <serveza/web/context.h>
#include <serveza/web/middleware.h>
#include <serveza/web/target.h>

#include "detail/http_date.h"

namespace serveza::web::middleware {

namespace {

namespace http = boost::beast::http;

std::string lowercase(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

std::string normalized_extension(std::string value)
{
  if (value.empty()) throw std::invalid_argument{"MIME extension must not be empty"};
  if (value.front() != '.') value.insert(value.begin(), '.');
  if (value.size() == 1 || value.find('/') != std::string::npos || value.find('\\') != std::string::npos ||
      value.find('\r') != std::string::npos || value.find('\n') != std::string::npos ||
      value.find('\0') != std::string::npos)
    throw std::invalid_argument{"MIME extension is invalid"};
  return lowercase(std::move(value));
}

void validate_content_type(std::string_view value)
{
  if (value.empty() || value.find_first_of("\r\n") != std::string_view::npos)
    throw std::invalid_argument{"content type is invalid"};
}

bool is_safe_relative_path(std::string_view value, bool allow_hidden) noexcept
{
  if ((!value.empty() && value.front() == '/') || value.find('\0') != std::string_view::npos ||
      value.find('\\') != std::string_view::npos)
    return false;

  while (!value.empty()) {
    const auto separator = value.find('/');
    const auto segment = value.substr(0, separator);
    if (segment.empty() || segment == "." || segment == ".." || (!allow_hidden && segment.front() == '.')) return false;
    if (separator == std::string_view::npos) return true;
    value.remove_prefix(separator + 1);
    if (value.empty()) return true;
  }
  return true;
}

bool is_within(const std::filesystem::path& root, const std::filesystem::path& candidate) noexcept
{
  auto root_part = root.begin();
  auto candidate_part = candidate.begin();
  for (; root_part != root.end(); ++root_part, ++candidate_part) {
    if (candidate_part == candidate.end() || *root_part != *candidate_part) return false;
  }
  return true;
}

bool contains_hidden_component(const std::filesystem::path& value)
{
  for (const auto& component : value) {
    const auto text = component.string();
    if (!text.empty() && text.front() == '.') return true;
  }
  return false;
}

std::chrono::system_clock::time_point system_time(std::filesystem::file_time_type value)
{
  const auto file_now = std::filesystem::file_time_type::clock::now();
  const auto system_now = std::chrono::system_clock::now();
  return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      system_now + std::chrono::duration_cast<std::chrono::system_clock::duration>(value - file_now));
}

template<typename T>
void append_hex(std::string& output, T value)
{
  static_assert(std::is_unsigned_v<T>);
  char buffer[sizeof(T) * 2];
  const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value, 16);
  if (converted.ec != std::errc{}) throw std::runtime_error{"could not format ETag"};
  output.append(buffer, converted.ptr);
}

std::string make_etag(std::filesystem::file_time_type modified, std::uintmax_t size)
{
  const auto ticks = modified.time_since_epoch().count();
  using unsigned_ticks = std::make_unsigned_t<std::remove_cv_t<decltype(ticks)>>;

  std::string result{"W/\""};
  result.reserve(5 + sizeof(ticks) * 2 + sizeof(size) * 2);
  append_hex(result, static_cast<unsigned_ticks>(ticks));
  result.push_back('-');
  append_hex(result, size);
  result.push_back('"');
  return result;
}

std::string_view trim(std::string_view value) noexcept
{
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
    value.remove_prefix(1);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
    value.remove_suffix(1);
  return value;
}

std::string_view weak_tag(std::string_view value) noexcept
{
  value = trim(value);
  if (value.size() >= 2 && (value[0] == 'W' || value[0] == 'w') && value[1] == '/') value.remove_prefix(2);
  return value;
}

bool etag_matches(std::string_view header, std::string_view current) noexcept
{
  while (!header.empty()) {
    const auto separator = header.find(',');
    const auto candidate = trim(header.substr(0, separator));
    if (candidate == "*" || weak_tag(candidate) == weak_tag(current)) return true;
    if (separator == std::string_view::npos) break;
    header.remove_prefix(separator + 1);
  }
  return false;
}

struct requested_range final {
  enum class state {
    ignored,
    unsatisfiable,
    valid,
  } result{state::ignored};

  std::uint64_t offset{};
  std::uint64_t length{};
};

bool parse_uint64(std::string_view value, std::uint64_t& result) noexcept
{
  if (value.empty()) return false;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

requested_range parse_range(std::string_view value, std::uint64_t size) noexcept
{
  value = trim(value);
  if (value.substr(0, 6) != "bytes=") return {};
  value.remove_prefix(6);
  if (value.empty() || value.find(',') != std::string_view::npos) return {};

  const auto dash = value.find('-');
  if (dash == std::string_view::npos || value.find('-', dash + 1) != std::string_view::npos) return {};

  const auto first = trim(value.substr(0, dash));
  const auto last = trim(value.substr(dash + 1));
  if (first.empty()) {
    std::uint64_t suffix{};
    if (!parse_uint64(last, suffix)) return {};
    if (suffix == 0 || size == 0) return {requested_range::state::unsatisfiable, 0, 0};
    const auto length = std::min(suffix, size);
    return {requested_range::state::valid, size - length, length};
  }

  std::uint64_t offset{};
  if (!parse_uint64(first, offset)) return {};
  if (offset >= size) return {requested_range::state::unsatisfiable, 0, 0};
  if (last.empty()) return {requested_range::state::valid, offset, size - offset};

  std::uint64_t end{};
  if (!parse_uint64(last, end)) return {};
  if (end < offset) return {requested_range::state::unsatisfiable, 0, 0};
  end = std::min(end, size - 1);
  return {requested_range::state::valid, offset, end - offset + 1};
}

bool if_range_matches(std::string_view value, std::chrono::system_clock::time_point modified) noexcept
{
  value = trim(value);
  if (value.empty() || value.front() == '"' || value.substr(0, 2) == "W/") return false;
  const auto date = web::details::parse_http_date(value);
  return date && modified <= std::chrono::time_point_cast<std::chrono::seconds>(*date);
}

std::string_view header_value(const http::fields& fields, http::field name) noexcept
{
  const auto value = fields[name];
  return {value.data(), value.size()};
}

static_files_options route_options(std::string route_parameter)
{
  static_files_options result;
  result.route_parameter = std::move(route_parameter);
  return result;
}

} // namespace

struct mime_type_registry::implementation final {
  std::unordered_map<std::string, std::string> mappings;
  std::string fallback{"application/octet-stream"};
};

mime_type_registry::mime_type_registry()
  : m_impl{std::make_unique<implementation>()}
{
  const std::pair<std::string_view, std::string_view> defaults[] = {
      {".7z", "application/x-7z-compressed"},
      {".atom", "application/atom+xml"},
      {".avif", "image/avif"},
      {".bmp", "image/bmp"},
      {".css", "text/css; charset=utf-8"},
      {".csv", "text/csv; charset=utf-8"},
      {".eot", "application/vnd.ms-fontobject"},
      {".gif", "image/gif"},
      {".gz", "application/gzip"},
      {".htm", "text/html; charset=utf-8"},
      {".html", "text/html; charset=utf-8"},
      {".ico", "image/x-icon"},
      {".jpeg", "image/jpeg"},
      {".jpg", "image/jpeg"},
      {".js", "text/javascript; charset=utf-8"},
      {".json", "application/json"},
      {".map", "application/json"},
      {".md", "text/markdown; charset=utf-8"},
      {".mjs", "text/javascript; charset=utf-8"},
      {".mov", "video/quicktime"},
      {".mp3", "audio/mpeg"},
      {".mp4", "video/mp4"},
      {".ogg", "audio/ogg"},
      {".opus", "audio/opus"},
      {".otf", "font/otf"},
      {".pdf", "application/pdf"},
      {".png", "image/png"},
      {".rar", "application/vnd.rar"},
      {".rss", "application/rss+xml"},
      {".svg", "image/svg+xml"},
      {".tar", "application/x-tar"},
      {".tar.gz", "application/gzip"},
      {".tif", "image/tiff"},
      {".tiff", "image/tiff"},
      {".ttf", "font/ttf"},
      {".txt", "text/plain; charset=utf-8"},
      {".wasm", "application/wasm"},
      {".wav", "audio/wav"},
      {".webm", "video/webm"},
      {".webmanifest", "application/manifest+json"},
      {".webp", "image/webp"},
      {".woff", "font/woff"},
      {".woff2", "font/woff2"},
      {".xml", "application/xml"},
      {".zip", "application/zip"},
  };
  for (const auto& [extension, type] : defaults)
    m_impl->mappings.emplace(extension, type);
}

mime_type_registry::mime_type_registry(const mime_type_registry& other)
  : m_impl{std::make_unique<implementation>(*other.m_impl)}
{}

mime_type_registry::mime_type_registry(mime_type_registry&& other) noexcept = default;

mime_type_registry& mime_type_registry::operator=(const mime_type_registry& other)
{
  if (this != &other) m_impl = std::make_unique<implementation>(*other.m_impl);
  return *this;
}

mime_type_registry& mime_type_registry::operator=(mime_type_registry&& other) noexcept = default;

mime_type_registry::~mime_type_registry() = default;

mime_type_registry& mime_type_registry::set(std::string extension, std::string content_type)
{
  validate_content_type(content_type);
  m_impl->mappings[normalized_extension(std::move(extension))] = std::move(content_type);
  return *this;
}

bool mime_type_registry::erase(std::string_view extension)
{
  return m_impl->mappings.erase(normalized_extension(std::string{extension})) != 0;
}

mime_type_registry& mime_type_registry::set_fallback(std::string content_type)
{
  validate_content_type(content_type);
  m_impl->fallback = std::move(content_type);
  return *this;
}

std::string_view mime_type_registry::find(const std::filesystem::path& path) const
{
  const auto filename = lowercase(path.filename().string());
  auto dot = filename.find('.');
  while (dot != std::string::npos) {
    const auto found = m_impl->mappings.find(filename.substr(dot));
    if (found != m_impl->mappings.end()) return found->second;
    dot = filename.find('.', dot + 1);
  }
  return m_impl->fallback;
}

std::string_view mime_type_registry::fallback() const noexcept
{
  return m_impl->fallback;
}

static_files::static_files(std::filesystem::path root, std::string route_parameter)
  : static_files{std::move(root), route_options(std::move(route_parameter))}
{}

static_files::static_files(std::filesystem::path root, static_files_options options)
  : m_options{std::move(options)}
{
  if (m_options.route_parameter.empty()) throw std::invalid_argument{"static file route parameter must not be empty"};
  if (m_options.cache_control.find_first_of("\r\n") != std::string::npos)
    throw std::invalid_argument{"static file Cache-Control value is invalid"};
  for (const auto& index : m_options.index_files) {
    if (!is_safe_relative_path(index, m_options.serve_hidden_files))
      throw std::invalid_argument{"static file index name must be a safe relative path"};
  }

  std::error_code fsEc;
  m_root = std::filesystem::canonical(std::move(root), fsEc);
  if (fsEc || !std::filesystem::is_directory(m_root, fsEc) || fsEc)
    throw std::invalid_argument{"static file root must be an existing directory"};
}

const std::filesystem::path& static_files::root() const noexcept
{
  return m_root;
}

const std::string& static_files::route_parameter() const noexcept
{
  return m_options.route_parameter;
}

const static_files_options& static_files::options() const noexcept
{
  return m_options;
}

void static_files::operator()(request_context& ctx, web::continuation& next, completion_handler handler) const
{
  if (ctx.method() != http::verb::get && ctx.method() != http::verb::head) {
    next(std::move(handler));
    return;
  }

  const auto encoded = ctx.param(m_options.route_parameter);
  if (!encoded) {
    next(std::move(handler));
    return;
  }

  const auto decoded = percent_decode(*encoded);
  if (!is_safe_relative_path(decoded, m_options.serve_hidden_files)) {
    next(std::move(handler));
    return;
  }

  std::error_code fsEc;
  const auto relative = std::filesystem::u8path(decoded);
  if (relative.has_root_path()) {
    next(std::move(handler));
    return;
  }
  auto candidate = std::filesystem::canonical(m_root / relative, fsEc);
  if (fsEc || !is_within(m_root, candidate) ||
      (!m_options.serve_hidden_files && contains_hidden_component(candidate.lexically_relative(m_root)))) {
    next(std::move(handler));
    return;
  }

  if (std::filesystem::is_directory(candidate, fsEc) && !fsEc) {
    bool found{};
    for (const auto& index : m_options.index_files) {
      auto indexed = std::filesystem::canonical(candidate / std::filesystem::u8path(index), fsEc);
      if (!fsEc && is_within(m_root, indexed) && std::filesystem::is_regular_file(indexed, fsEc) && !fsEc) {
        candidate = std::move(indexed);
        found = true;
        break;
      }
      fsEc.clear();
    }
    if (!found) {
      next(std::move(handler));
      return;
    }
  } else if (fsEc || !std::filesystem::is_regular_file(candidate, fsEc) || fsEc) {
    next(std::move(handler));
    return;
  }

  const auto file_size = std::filesystem::file_size(candidate, fsEc);
  if (fsEc) {
    next(std::move(handler));
    return;
  }
  const auto modified_file_time = std::filesystem::last_write_time(candidate, fsEc);
  if (fsEc) {
    next(std::move(handler));
    return;
  }
  const auto modified = std::chrono::time_point_cast<std::chrono::seconds>(system_time(modified_file_time));
  const auto etag = make_etag(modified_file_time, file_size);
  const auto last_modified = web::details::format_http_date(modified);

  auto& response_headers = ctx.response_headers();
  const auto original_headers = response_headers;
  if (m_options.etag) response_headers.set(http::field::etag, etag);
  if (m_options.last_modified) response_headers.set(http::field::last_modified, last_modified);
  if (m_options.byte_ranges) response_headers.set(http::field::accept_ranges, "bytes");
  if (!m_options.cache_control.empty()) response_headers.set(http::field::cache_control, m_options.cache_control);

  const auto& request_headers = ctx.request_headers();
  const auto if_none_match = header_value(request_headers, http::field::if_none_match);
  if (m_options.etag && !if_none_match.empty()) {
    if (etag_matches(if_none_match, etag)) {
      ctx.async_send_status(http::status::not_modified, std::move(handler));
      return;
    }
  } else if (m_options.last_modified) {
    const auto if_modified_since = header_value(request_headers, http::field::if_modified_since);
    const auto since = web::details::parse_http_date(if_modified_since);
    if (since && modified <= std::chrono::time_point_cast<std::chrono::seconds>(*since)) {
      ctx.async_send_status(http::status::not_modified, std::move(handler));
      return;
    }
  }

  if (m_options.byte_ranges && ctx.method() == http::verb::get) {
    const auto range_header = header_value(request_headers, http::field::range);
    const auto if_range = header_value(request_headers, http::field::if_range);
    if (!range_header.empty() && (if_range.empty() || if_range_matches(if_range, modified))) {
      const auto range = parse_range(range_header, static_cast<std::uint64_t>(file_size));
      if (range.result == requested_range::state::unsatisfiable) {
        response_headers.set(http::field::content_range, "bytes */" + std::to_string(file_size));
        ctx.async_send_status(http::status::range_not_satisfiable, std::move(handler));
        return;
      }
      if (range.result == requested_range::state::valid) {
        const auto end = range.offset + range.length - 1;
        response_headers.set(http::field::content_range, "bytes " + std::to_string(range.offset) + '-' +
                                                             std::to_string(end) + '/' + std::to_string(file_size));
        ctx.async_send_file_range(http::status::partial_content, candidate, range.offset, range.length,
                                  m_options.content_types.find(candidate),
                                  [&ctx, &next, original_headers,
                                   handler = std::move(handler)](boost::system::error_code ec, bool sent) mutable {
                                    if (ec || sent) {
                                      handler(ec);
                                      return;
                                    }
                                    ctx.response_headers() = original_headers;
                                    next(std::move(handler));
                                  });
        return;
      }
    }
  }

  ctx.async_send_file(
      http::status::ok, candidate, m_options.content_types.find(candidate),
      [&ctx, &next, original_headers, handler = std::move(handler)](boost::system::error_code ec, bool sent) mutable {
        if (ec || sent) {
          handler(ec);
          return;
        }
        ctx.response_headers() = original_headers;
        next(std::move(handler));
      });
}

} // namespace serveza::web::middleware

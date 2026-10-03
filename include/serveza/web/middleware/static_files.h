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

#ifndef SERVEZA_WEB_MIDDLEWARE_STATIC_FILES_H
#define SERVEZA_WEB_MIDDLEWARE_STATIC_FILES_H

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <serveza/web/async.h>
#include <serveza/web/export.h>

namespace serveza::web {

class request_context;
class continuation;

namespace middleware {

/**
 * @brief Extension-to-content-type registry used by static file middleware.
 *
 * Extension lookup is case-insensitive. A leading dot is optional when adding
 * or removing a mapping. Compound extensions such as @c tar.gz are supported.
 */
class SERVEZA_WEB_API mime_type_registry final {
public:
  /** @brief Creates a registry populated with common web media types. */
  mime_type_registry();

  /** @brief Copies a registry and all of its mappings. */
  mime_type_registry(const mime_type_registry& other);

  /** @brief Moves a registry. */
  mime_type_registry(mime_type_registry&& other) noexcept;

  /** @brief Replaces this registry with a copy of @p other. */
  mime_type_registry& operator=(const mime_type_registry& other);

  /** @brief Replaces this registry with @p other. */
  mime_type_registry& operator=(mime_type_registry&& other) noexcept;

  /** @brief Destroys the registry. */
  ~mime_type_registry();

  /**
   * @brief Adds or replaces the mapping for @p extension.
   * @return This registry, for builder-style configuration.
   * @throws std::invalid_argument if either value is invalid.
   */
  mime_type_registry& set(std::string extension, std::string content_type);

  /** @brief Removes the mapping for @p extension and reports whether it existed. */
  bool erase(std::string_view extension);

  /**
   * @brief Sets the content type returned when no extension matches.
   * @return This registry, for builder-style configuration.
   * @throws std::invalid_argument if @p content_type is empty or contains a line break.
   */
  mime_type_registry& set_fallback(std::string content_type);

  /** @brief Returns the best content type for @p path. */
  [[nodiscard]] std::string_view find(const std::filesystem::path& path) const;

  /** @brief Returns the content type used when no extension matches. */
  [[nodiscard]] std::string_view fallback() const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> m_impl;
};

/** @brief Configuration for @ref static_files. */
struct static_files_options {
  /** @brief Wildcard route parameter containing the requested relative path. */
  std::string route_parameter{"path"};

  /** @brief Files tried in order when the requested path is a directory. */
  std::vector<std::string> index_files{"index.html", "index.htm"};

  /** @brief Content types used for served files. */
  mime_type_registry content_types;

  /** @brief Whether paths containing a dot-prefixed component may be served. */
  bool serve_hidden_files{false};

  /** @brief Whether to emit ETag and process If-None-Match. */
  bool etag{true};

  /** @brief Whether to emit Last-Modified and process If-Modified-Since. */
  bool last_modified{true};

  /** @brief Whether GET requests may select one byte range. */
  bool byte_ranges{true};

  /** @brief Optional Cache-Control response field value. */
  std::string cache_control;
};

/**
 * @brief Terminal-or-fallthrough middleware that streams regular files.
 *
 * GET and HEAD requests are served from a canonical document root. The
 * middleware rejects path traversal and escaping symlinks, supports directory
 * index files, emits cache validators, honors conditional GET requests, and
 * streams single byte ranges. Missing or unsafe paths fall through to the next
 * matching handler.
 */
class SERVEZA_WEB_API static_files final {
public:
  /**
   * @brief Creates file middleware rooted at @p root.
   * @throws std::invalid_argument if @p root is not an existing directory.
   */
  explicit static_files(std::filesystem::path root, std::string route_parameter = "path");

  /**
   * @brief Creates file middleware rooted at @p root with @p options.
   * @throws std::invalid_argument if the root or options are invalid.
   */
  static_files(std::filesystem::path root, static_files_options options);

  /** @brief Streams a safe matching file, sends 304, or continues the chain. */
  void operator()(request_context& ctx, web::continuation& next, completion_handler handler) const;

  /** @brief Returns the canonical document root. */
  [[nodiscard]] const std::filesystem::path& root() const noexcept;

  /** @brief Returns the route parameter containing the relative file path. */
  [[nodiscard]] const std::string& route_parameter() const noexcept;

  /** @brief Returns the immutable middleware configuration. */
  [[nodiscard]] const static_files_options& options() const noexcept;

private:
  std::filesystem::path m_root;
  static_files_options m_options;
};

} // namespace middleware
} // namespace serveza::web

#endif // SERVEZA_WEB_MIDDLEWARE_STATIC_FILES_H

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

#ifndef SERVEZA_WEB_EXPORT_H
#define SERVEZA_WEB_EXPORT_H

#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(SERVEZA_WEB_LIBRARY_BUILD)
#define SERVEZA_WEB_API __declspec(dllexport)
#elif defined(SERVEZA_WEB_USE_SHARED)
#define SERVEZA_WEB_API __declspec(dllimport)
#else
#define SERVEZA_WEB_API
#endif
#elif defined(__GNUC__) && defined(SERVEZA_WEB_LIBRARY_BUILD)
#define SERVEZA_WEB_API __attribute__((visibility("default")))
#else
#define SERVEZA_WEB_API
#endif

#endif // SERVEZA_WEB_EXPORT_H

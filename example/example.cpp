#include <functional>
#include <string>
#include <tuple>
#include <vector>

#define EXAMPLE_MACRO_CONST 123
#define EXAMPLE_MACRO_FUNC(x) ((x) + 1)
#define EXAMPLE_MACRO_VARARGS_1(a, b, ...) ((a)((b), __VA_ARGS__))
#define EXAMPLE_MACRO_VARARGS_2(...) ((void)(__VA_ARGS__))

#define EXAMPLE_MACRO_DEFINE_STRUCT(name)                                                                              \
  struct macro_##name {};

#define EXAMPLE_MACRO_DEFINE_OPERATOR(name, ret, op) ret operator op(const name&) const;

namespace example {

EXAMPLE_MACRO_DEFINE_STRUCT(struct)

struct struct_with_macro_expansions {
  EXAMPLE_MACRO_DEFINE_STRUCT(nested_struct)
  EXAMPLE_MACRO_DEFINE_OPERATOR(struct_with_macro_expansions, bool, ==)
  EXAMPLE_MACRO_DEFINE_OPERATOR(struct_with_macro_expansions, bool, !=)
};

enum unscoped_enum { u_value1 = -1, u_value2, u_value3, u_value4 };
enum unscoped_int_enum : int { ui_value1 = -1, ui_value2, ui_value3, ui_value4 };
enum class scoped_enum { value1, value2, value3 = value2, value4 };
enum class scoped_int_enum : int { value1, value2, value3 = value2, value4 };

using alias = int;
typedef int alias_typedef;
template <typename T> using alias_template                                 = std::vector<T>;
template <typename... T> using alias_variadic_template                     = std::tuple<T...>;
template <template <typename...> typename T> using alias_template_template = T<int>;

struct forward_struct;
template <typename T> struct forward_struct_template;
template <typename T> using forward_struct_value_type = typename forward_struct_template<T>::value_type;

class class_with_deleted_special_members {
public:
  class_with_deleted_special_members()                                                     = delete;
  class_with_deleted_special_members(const class_with_deleted_special_members&)            = delete;
  class_with_deleted_special_members(class_with_deleted_special_members&&)                 = delete;
  class_with_deleted_special_members& operator=(const class_with_deleted_special_members&) = delete;
  class_with_deleted_special_members& operator=(class_with_deleted_special_members&&)      = delete;
  ~class_with_deleted_special_members()                                                    = delete;
};

class class_with_defaulted_special_members {
public:
  class_with_defaulted_special_members()                                                       = default;
  class_with_defaulted_special_members(const class_with_defaulted_special_members&)            = default;
  class_with_defaulted_special_members(class_with_defaulted_special_members&&)                 = default;
  class_with_defaulted_special_members& operator=(const class_with_defaulted_special_members&) = default;
  class_with_defaulted_special_members& operator=(class_with_defaulted_special_members&&)      = default;
  ~class_with_defaulted_special_members()                                                      = default;
};

class class_with_hidden_friends {
public:
  friend bool operator==(const class_with_hidden_friends& first, const class_with_hidden_friends& second);
  friend bool operator++(class_with_hidden_friends& self);
  friend void swap(class_with_hidden_friends& first, class_with_hidden_friends& second) noexcept;
  template <typename... Ts> friend auto fold(class_with_hidden_friends& head, Ts&&... tail);
};

template <typename T> class template_class_with_hidden_friends {
public:
  friend bool operator==(const template_class_with_hidden_friends& first,
                         const template_class_with_hidden_friends& second);
  friend bool operator++(template_class_with_hidden_friends& self);
  friend void swap(template_class_with_hidden_friends& first, template_class_with_hidden_friends& second) noexcept;
  template <typename... Ts> friend auto fold(template_class_with_hidden_friends& head, Ts&&... tail);
};

template <typename A, typename B, typename C> class template_class_with_hidden_friends<std::tuple<A, B, C>> {
public:
  friend bool operator==(const template_class_with_hidden_friends& first,
                         const template_class_with_hidden_friends& second);
  friend bool operator++(template_class_with_hidden_friends& self);
  friend void swap(template_class_with_hidden_friends& first, template_class_with_hidden_friends& second) noexcept;
  template <typename... Ts> friend auto fold(template_class_with_hidden_friends& head, Ts&&... tail);
};

template <> class template_class_with_hidden_friends<void> {
public:
  friend bool operator==(const template_class_with_hidden_friends& first,
                         const template_class_with_hidden_friends& second);
  friend bool operator++(template_class_with_hidden_friends& self);
  friend void swap(template_class_with_hidden_friends& first, template_class_with_hidden_friends& second) noexcept;
  template <typename... Ts> friend auto fold(template_class_with_hidden_friends& head, Ts&&... tail);
};

class abstract_class {
public:
  virtual ~abstract_class()       = 0;
  virtual void public_foo()       = 0;
  virtual void public_bar() const = 0;

protected:
  abstract_class()             = default;
  virtual void protected_baz() = 0;

private:
  virtual void private_qux() = 0;
};

class overriding_class : public abstract_class {
public:
  ~overriding_class() override;
  void public_foo() override;
  void public_bar() const final;
};

class final_class final : public overriding_class {
public:
  void public_foo() final;
};

struct struct_with_qualified_members {
  struct constexpr_tag;
  struct inline_tag;
  struct explicit_tag;
  struct noexcept_tag;

  struct_with_qualified_members() = default;
  struct_with_qualified_members(const struct_with_qualified_members&) noexcept;
  struct_with_qualified_members(struct_with_qualified_members&&) noexcept;
  struct_with_qualified_members& operator=(const struct_with_qualified_members&) noexcept;
  struct_with_qualified_members& operator=(struct_with_qualified_members&&) noexcept;
  ~struct_with_qualified_members() noexcept(false) = default;

  constexpr struct_with_qualified_members(constexpr_tag);
  inline struct_with_qualified_members(inline_tag);
  explicit struct_with_qualified_members(explicit_tag);
  struct_with_qualified_members(noexcept_tag) noexcept;

  int                  member;
  const int            const_member;
  mutable int          mutable_member;
  volatile int         volatile_member;
  const volatile int   const_volatile_member;
  mutable volatile int mutable_volatile_member;

  static int                static_member;
  static const int          static_const_member;
  static volatile int       static_volatile_member;
  static const volatile int static_const_volatile_member;

  inline static int                    inline_static_member;
  inline static const int              inline_static_const_member     = 0;
  inline static constexpr int          inline_static_constexpr_member = 0;
  inline static volatile int           inline_static_volatile_member;
  inline static const volatile int     inline_static_const_volatile_member     = 0;
  inline static constexpr volatile int inline_static_constexpr_volatile_member = 0;

  inline int    inline_member_fn();
  constexpr int inline_member_fn() const;
  int           noexcept_member_fn() noexcept;

  int value_member_fn();
  int value_member_fn() const;
  int value_member_fn() volatile;
  int value_member_fn() const volatile;

  int ref_member_fn() &;
  int ref_member_fn() const&;
  int ref_member_fn() volatile&;
  int ref_member_fn() const volatile&;
  int ref_member_fn() &&;
  int ref_member_fn() const&&;
  int ref_member_fn() volatile&&;
  int ref_member_fn() const volatile&&;

  operator int() const;

  operator float&();
  operator const float&() const;
  operator volatile float&() volatile;
  operator const volatile float&() const volatile;

  explicit operator bool() const;

  template <bool Noexcept> void conditionally_noexcept_member_fn() noexcept(Noexcept);
};

struct struct_with_aliases {
  using alias = int;
  typedef int alias_typedef;
  template <typename T> using vector                       = std::vector<T>;
  template <template <typename...> typename T> using apply = T<int>;
};

template <typename T, typename U> struct struct_template_with_aliases {
  using alias = T;
  typedef int alias_typedef;
  using value_type                                         = typename U::value_type;
  template <typename V> using vector                       = std::vector<V>;
  template <template <typename...> typename V> using apply = V<T, U>;
};

template <typename T> struct struct_template_with_aliases<T, void> {
  using alias = T;
  typedef int alias_typedef;
  using value_type                                         = typename T::value_type;
  template <typename V> using vector                       = std::vector<V>;
  template <template <typename...> typename V> using apply = V<T, T>;
};

template <> struct struct_template_with_aliases<void, void> {
  using alias = void;
  typedef int alias_typedef;
  template <typename V> using vector                       = std::vector<V>;
  template <template <typename...> typename V> using apply = V<void>;
};

template <typename T, typename U> using dependent_alias = typename T::template vector<U>;

struct struct_with_private_members {
protected:
  using protected_type = int;
  struct_with_private_members(int);
  int                        protected_member;
  template <typename T> void protected_member_fn(T);

private:
  using private_type = int;
  struct_with_private_members(int, int);
  int                        private_member;
  template <typename T> void private_member_fn(T);
};

struct struct_exporting_base_members : struct_with_private_members {
  using struct_with_private_members::protected_member;
  using struct_with_private_members::protected_member_fn;
  using struct_with_private_members::struct_with_private_members;
  using typename struct_with_private_members::protected_type;
};

struct struct_with_nested_struct {
  struct nested_struct {};
  template <typename T> struct nested_struct_template {};
};

template <typename T> struct template_struct_with_nested_struct {
  struct nested_struct {};
  template <typename U = T> struct nested_struct_template {};
};

template <typename... Ts> struct template_struct_with_nested_struct<std::tuple<Ts...>> {
  struct nested_struct {};
  template <typename U> struct nested_struct_template {};
};

template <> struct template_struct_with_nested_struct<void> {
  struct nested_struct {};
  template <typename U> struct nested_struct_template {};
};

struct struct_with_friends {
  friend void                       undeclared_friend_fn();
  template <typename T> friend void undeclared_template_friend_fn(T);
};

struct forward_partial_specialization;
struct defined_partial_specialization;
struct full_specialization;

template <typename...> struct struct_template;

template <typename... T> struct struct_template<forward_partial_specialization, T...>;

template <typename... T> struct struct_template<defined_partial_specialization, T...> {
  using next = struct_template<T...>;
  using type = typename next::type;
};

template <> struct struct_template<full_specialization> {
  using type = int;
};

template <template <typename...> typename T> struct struct_template_over_templates {
  template <typename... U> using type = T<U...>;
};

template <> struct struct_template_over_templates<std::tuple> {
  using type = void;
};

}; // namespace example

namespace example::functions {

void               function();
int                function_returns();
const int          function_returns_const();
volatile int       function_returns_volatile();
const volatile int function_returns_const_volatile();
auto               function_with_trailing_ret() -> int;
auto               function_with_auto_ret() -> int;
decltype(auto)     function_with_decltype_auto_ret();
decltype(0)        function_with_decltype_ret();

template <typename... Ts> void                    function_template(Ts const&...);
template <> void                                  function_template<int, char>(int const&, char const&);
template <template <typename...> typename T> void function_template_over_templates(T<int, float, void>&& v);
template <>
void function_template_over_templates<alias_variadic_template>(alias_variadic_template<int, float, void>&& v);

template <typename T> auto function_with_decltype_ret(T a, T b) -> decltype(a + b);

void                                     noexcept_function() noexcept;
[[noreturn]] void                        noreturn_function();
[[nodiscard]] int                        nodiscard_function();
[[nodiscard("nodiscard reason")]] int    nodiscard_reason_function();
[[deprecated]] int                       deprecated_function();
[[deprecated("deprecation reason")]] int deprecated_reason_function();

void function_with_qualified_args(int                i,
                                  const int          ci,
                                  volatile int       vi,
                                  const volatile int cvi,
                                  int*               pi,
                                  int* const         cpi,
                                  int* volatile vpi,
                                  int* const volatile cvpi,
                                  const int*          pci,
                                  volatile int*       pvi,
                                  const volatile int* pcvi,
                                  int&                ri,
                                  const int&          rci,
                                  volatile int&       rvi,
                                  const volatile int& rcvi);

template <int                I,
          const int          CI,
          volatile int       VI,
          const volatile int CVI,
          int*               PI,
          int* const         CPI,
          int* volatile VPI,
          int* const volatile CVPI,
          const int*          PCI,
          volatile int*       PVI,
          const volatile int* PCVI,
          int&                RI,
          const int&          RCI,
          volatile int&       RVI,
          const volatile int& RCVI>
void template_function_with_qualified_args();

void function_with_unused_args([[maybe_unused]] int p, int /* q */);

} // namespace example::functions

namespace example::variables {

int        global_variable;
extern int global_extern_variable;
static int global_static_variable;

template <int V> int                  global_variable_template                  = V;
template <int V> const int            global_const_variable_template            = V;
template <int V> constexpr int        global_constexpr_variable_template        = V;
template <int V> inline constexpr int global_inline_constexpr_variable_template = V;

template <typename T> constexpr size_t     specialized_global_variable                    = sizeof(T);
template <typename... Ts> constexpr size_t specialized_global_variable<std::tuple<Ts...>> = (sizeof(Ts) + ...);
template <> constexpr size_t               specialized_global_variable<void>              = 0;
// TODO can a record have specialized static member variables?

} // namespace example::variables

namespace example::detail {
struct detail_struct {};
} // namespace example::detail

namespace example::other::detail {
struct other_struct {};
} // namespace example::other::detail

namespace example::other::detail {
struct other_detail_struct {};
} // namespace example::other::detail

namespace example::detail::nested_detail {
struct nested_detail_struct {};
} // namespace example::detail::nested_detail

struct global_namespace_struct {};
int global_variable;

template <> struct std::hash<example::unscoped_enum> {
  std::size_t operator()(example::unscoped_enum value) const;
};

template <typename... T> struct std::hash<example::struct_template<T...>> {
  std::size_t operator()(example::struct_template<T...> value) const;
};

template <template <typename...> typename T> struct std::hash<example::struct_template_over_templates<T>> {
  std::size_t operator()(example::struct_template_over_templates<T> value) const;
};

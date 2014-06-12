/*************************************************************************
 *
 * TIGHTDB CONFIDENTIAL
 * __________________
 *
 *  [2011] - [2012] TightDB Inc
 *  All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains
 * the property of TightDB Incorporated and its suppliers,
 * if any.  The intellectual and technical concepts contained
 * herein are proprietary to TightDB Incorporated
 * and its suppliers and may be covered by U.S. and Foreign Patents,
 * patents in process, and are protected by trade secret or copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from TightDB Incorporated.
 *
 **************************************************************************/
#ifndef TIGHTDB_TABLE_HPP
#define TIGHTDB_TABLE_HPP

#include <utility>

#include <tightdb/util/features.h>
#include <tightdb/util/tuple.hpp>
#include <tightdb/column_fwd.hpp>
#include <tightdb/table_ref.hpp>
#include <tightdb/link_view_fwd.hpp>
#include <tightdb/row.hpp>
#include <tightdb/descriptor_fwd.hpp>
#include <tightdb/spec.hpp>
#include <tightdb/mixed.hpp>
#include <tightdb/query.hpp>

namespace tightdb {

class TableView;
class TableViewBase;
class ConstTableView;
class StringIndex;
class Group;
class ColumnBackLink;
class ColumnLinkBase;

template<class> class Columns;

namespace _impl { class TableFriend; }

#ifdef TIGHTDB_ENABLE_REPLICATION
class Replication;
#endif


/// The Table class is non-polymorphic, that is, it has no virtual
/// functions. This is important because it ensures that there is no
/// run-time distinction between a Table instance and an instance of
/// any variation of BasicTable<T>, and this, in turn, makes it valid
/// to cast a pointer from Table to BasicTable<T> even when the
/// instance is constructed as a Table. Of course, this also assumes
/// that BasicTable<> is non-polymorphic, has no destructor, and adds
/// no extra data members.
///
/// FIXME: Table assignment (from any group to any group) could be made
/// aliasing safe as follows: Start by cloning source table into
/// target allocator. On success, assign, and then deallocate any
/// previous structure at the target.
///
/// FIXME: It might be desirable to have a 'table move' feature
/// between two places inside the same group (say from a subtable or a
/// mixed column to group level). This could be done in a very
/// efficient manner.
///
/// FIXME: When compiling in debug mode, all public table methods
/// should TIGHTDB_ASSERT(is_attached()).
class Table {
public:
    /// Construct a new freestanding top-level table with static
    /// lifetime.
    ///
    /// This constructor should be used only when placing a table
    /// instance on the stack, and it is then the responsibility of
    /// the application that there are no objects of type TableRef or
    /// ConstTableRef that refer to it, or to any of its subtables,
    /// when it goes out of scope. To create a top-level table with
    /// dynamic lifetime, use Table::create() instead.
    Table(Allocator& = Allocator::get_default());

    /// Construct a copy of the specified table as a new freestanding
    /// top-level table with static lifetime.
    ///
    /// This constructor should be used only when placing a table
    /// instance on the stack, and it is then the responsibility of
    /// the application that there are no objects of type TableRef or
    /// ConstTableRef that refer to it, or to any of its subtables,
    /// when it goes out of scope. To create a top-level table with
    /// dynamic lifetime, use Table::copy() instead.
    Table(const Table&, Allocator& = Allocator::get_default());

    ~Table() TIGHTDB_NOEXCEPT;

    /// Construct a new freestanding top-level table with dynamic lifetime.
    static TableRef create(Allocator& = Allocator::get_default());

    /// Construct a copy of the specified table as a new freestanding top-level
    /// table with dynamic lifetime.
    TableRef copy(Allocator& = Allocator::get_default()) const;

    /// Returns true if, and only if this accessor is currently attached to an
    /// underlying table.
    ///
    /// A table accessor may get detached from the underlying row for various
    /// reasons (see below). When it does, it no longer refers to anything, and
    /// can no longer be used, except for calling is_attached(). The
    /// consequences of calling other methods on a detached table accessor are
    /// undefined. Table accessors obtained by calling functions in the TightDB
    /// API are always in the 'attached' state immediately upon return from
    /// those functions.
    ///
    /// A table accessor of a free-standing table never becomes detached (except
    /// during its eventual destruction). A group-level table accessor becomes
    /// detached if the underlying table is removed from the group (not
    /// currently possible), or when the group accessor is destroyed. A subtable
    /// accessor becomes detached if the underlying subtable is removed, or if
    /// the parent table accessor is detached. A table accessor does not become
    /// detached for any other reason than those mentioned here.
    ///
    /// FIXME: High level language bindings will probably want to be able to
    /// explicitely detach a group and all tables of that group if any modifying
    /// operation fails (e.g. memory allocation failure) (and something similar
    /// for freestanding tables) since that leaves the group in state where any
    /// further access is disallowed. This way they will be able to reliably
    /// intercept any attempt at accessing such a failed group.
    ///
    /// FIXME: The C++ documentation must state that if any modifying operation
    /// on a group (incl. tables, subtables, and specs) or on a free standing
    /// table (incl. subtables and specs) fails, then any further access to that
    /// group (except ~Group()) or freestanding table (except ~Table()) has
    /// undefined behaviour and is considered an error on behalf of the
    /// application. Note that even Table::is_attached() is disallowed in this
    /// case.
    bool is_attached() const TIGHTDB_NOEXCEPT;

    /// Get the name of this table, if it has any. Tables have names when, and
    /// only when they are direct members of groups. For a table of any other
    /// kind, this function returns the empty string.
    StringData get_name() const TIGHTDB_NOEXCEPT;

    //@{
    /// Conventience methods for inspecting the dynamic table type.
    ///
    /// These methods behave as if they were called on the descriptor
    /// returned by get_descriptor().
    std::size_t get_column_count() const TIGHTDB_NOEXCEPT;
    DataType    get_column_type(std::size_t column_ndx) const TIGHTDB_NOEXCEPT;
    StringData  get_column_name(std::size_t column_ndx) const TIGHTDB_NOEXCEPT;
    std::size_t get_column_index(StringData name) const TIGHTDB_NOEXCEPT;
    //@}

    //@{
    /// Convenience methods for manipulating the dynamic table type.
    ///
    /// These function must be called only for tables with independent
    /// dynamic type. A table has independent dynamic type if the
    /// function has_shared_type() returns false. A table that is a
    /// direct member of a group has independent dynamic type. So does
    /// a free-standing table, and a subtable in a column of type
    /// 'mixed'. All other tables have shared dynamic type. The
    /// consequences of calling any of these functions for a table
    /// with shared dynamic type are undefined.
    ///
    /// Apart from that, these methods behave as if they were called on the
    /// descriptor returned by get_descriptor(). Note especially that the
    /// `_link` suffixed functions must be used when inserting link-type
    /// columns.
    ///
    /// If you need to change the shared dynamic type of the subtables
    /// in a subtable column, consider using the API offered by the
    /// Descriptor class.
    ///
    /// \param subdesc If a non-null pointer is passed, and the
    /// specified type is `type_Table`, then this function
    /// automatically reteives the descriptor associated with the new
    /// subtable column, and stores a reference to its accessor in
    /// `*subdesc`.
    ///
    /// \return The value returned by add_column() and add_column_link(), is the
    /// index of the added column.
    ///
    /// \sa has_shared_type()
    /// \sa get_descriptor()
    /// \sa Descriptor
    std::size_t add_column(DataType type, StringData name, DescriptorRef* subdesc = 0);
    void insert_column(std::size_t column_ndx, DataType type, StringData name,
                       DescriptorRef* subdesc = 0);
    std::size_t add_column_link(DataType type, StringData name, Table& target);
    void insert_column_link(std::size_t column_ndx, DataType type, StringData name, Table& target);
    void remove_column(std::size_t column_ndx);
    void rename_column(std::size_t column_ndx, StringData new_name);
    //@}

    bool has_index(std::size_t column_ndx) const TIGHTDB_NOEXCEPT;

    /// Add a search index to the specified column of this table. This table
    /// must be a root table; that is, it must have an independent
    /// descriptor. Freestanding tables, group-level tables, and subtables in a
    /// column of type 'mixed' are all examples of root tables. See add_column()
    /// for more on this.
    ///
    /// \sa add_column()
    ///
    /// FIXME: Rename to add_search_index().
    void set_index(std::size_t column_ndx);

    //@{
    /// Get the dynamic type descriptor for this table.
    ///
    /// Every table has an associated descriptor that specifies its
    /// dynamic type. For simple tables, that is, tables without
    /// subtable columns, the dynamic type can be inspected and
    /// modified directly using methods such as get_column_count() and
    /// add_column(). For more complex tables, the type is best
    /// managed through the associated descriptor object which is
    /// returned by this method.
    ///
    /// \sa has_shared_type()
    DescriptorRef get_descriptor();
    ConstDescriptorRef get_descriptor() const;
    //@}

    //@{
    /// Get the dynamic type descriptor for the column with the
    /// specified index. That column must have type 'table'.
    ///
    /// This is merely a shorthand for calling
    /// `get_subdescriptor(column_ndx)` on the descriptor
    /// returned by `get_descriptor()`.
    DescriptorRef get_subdescriptor(std::size_t column_ndx);
    ConstDescriptorRef get_subdescriptor(std::size_t column_ndx) const;
    //@}

    //@{
    /// Get access to an arbitrarily nested dynamic type descriptor.
    ///
    /// The returned descriptor is the one you would get by calling
    /// Descriptor::get_subdescriptor() once for each entry in the
    /// specified path, starting with the descriptor returned by
    /// get_descriptor(). The path is allowed to be empty.
    typedef std::vector<std::size_t> path_vec;
    DescriptorRef get_subdescriptor(const path_vec& path);
    ConstDescriptorRef get_subdescriptor(const path_vec& path) const;
    //@}

    //@{
    /// Convenience methods for manipulating nested table types.
    ///
    /// These functions behave as if they were called on the
    /// descriptor returned by `get_subdescriptor(path)`. These
    /// function must be called only on tables with independent
    /// dynamic type.
    ///
    /// \return The value returned by add_subcolumn(), is the index of
    /// the added column within the descriptor referenced by the
    /// specified path.
    ///
    /// \sa Descriptor::add_column()
    /// \sa has_shared_type()
    std::size_t add_subcolumn(const path_vec& path, DataType type, StringData name);
    void insert_subcolumn(const path_vec& path, std::size_t column_ndx,
                          DataType type, StringData name);
    void remove_subcolumn(const path_vec& path, std::size_t column_ndx);
    void rename_subcolumn(const path_vec& path, std::size_t column_ndx, StringData new_name);
    //@}

    /// Does this table share its type with other tables?
    ///
    /// Tables that are direct members of groups have independent
    /// dynamic types. The same is true for free-standing tables and
    /// subtables in coulmns of type 'mixed'. For such tables, this
    /// function returns false.
    ///
    /// When a table has a column of type 'table', the cells in that
    /// column contain subtables. All those subtables have the same
    /// dynamic type, and they share a single type descriptor. For all
    /// such subtables, this function returns true. See
    /// Descriptor::is_root() for more on this.
    ///
    /// Please note that Table methods that modify the dynamic type
    /// directly, such as add_column(), are only allowed to be used on
    /// tables with non-shared type. If you need to modify a shared
    /// type, you will have to do that through the descriptor returned
    /// by get_descriptor(), but note that it will then affect all the
    /// tables sharing that descriptor.
    ///
    /// \sa get_descriptor()
    /// \sa Descriptor::is_root()
    bool has_shared_type() const TIGHTDB_NOEXCEPT;


    template<class T> Columns<T> column(std::size_t column); // FIXME: Should this one have been declared TIGHTDB_NOEXCEPT?

    // Table size and deletion
    bool        is_empty() const TIGHTDB_NOEXCEPT;
    std::size_t size() const TIGHTDB_NOEXCEPT;
    void        clear();

    typedef BasicRowExpr<Table> RowExpr;
    typedef BasicRowExpr<const Table> ConstRowExpr;

    RowExpr get(std::size_t row_ndx) TIGHTDB_NOEXCEPT;
    ConstRowExpr get(std::size_t row_ndx) const TIGHTDB_NOEXCEPT;

    RowExpr front() TIGHTDB_NOEXCEPT;
    ConstRowExpr front() const TIGHTDB_NOEXCEPT;

    RowExpr back() TIGHTDB_NOEXCEPT;
    ConstRowExpr back() const TIGHTDB_NOEXCEPT;

    RowExpr operator[](std::size_t row_ndx) TIGHTDB_NOEXCEPT;
    ConstRowExpr operator[](std::size_t row_ndx) const TIGHTDB_NOEXCEPT;

    // Row handling
    std::size_t add_empty_row(std::size_t num_rows = 1);
    void        insert_empty_row(std::size_t row_ndx, std::size_t num_rows = 1);
    void        remove(std::size_t row_ndx);
    void        remove_last();

    /// Move the last row to the specified index. This overwrites the target row
    /// and reduces the number of rows by one. If the target row is the last one
    /// it will just be deleted.
    void move_last_over(std::size_t target_row_ndx);

    // Insert row
    // NOTE: You have to insert values in ALL columns followed by insert_done().
    void insert_int(std::size_t column_ndx, std::size_t row_ndx, int64_t value);
    void insert_bool(std::size_t column_ndx, std::size_t row_ndx, bool value);
    void insert_datetime(std::size_t column_ndx, std::size_t row_ndx, DateTime value);
    template<class E> void insert_enum(std::size_t column_ndx, std::size_t row_ndx, E value);
    void insert_float(std::size_t column_ndx, std::size_t row_ndx, float value);
    void insert_double(std::size_t column_ndx, std::size_t row_ndx, double value);
    void insert_string(std::size_t column_ndx, std::size_t row_ndx, StringData value);
    void insert_binary(std::size_t column_ndx, std::size_t row_ndx, BinaryData value);
    void insert_subtable(std::size_t column_ndx, std::size_t row_ndx); // Insert empty table
    void insert_mixed(std::size_t column_ndx, std::size_t row_ndx, Mixed value);
    void insert_link(std::size_t column_ndx, std::size_t row_ndx, std::size_t target_row_ndx);
    void insert_linklist(std::size_t column_ndx, std::size_t row_ndx); // Insert empty link list
    void insert_done();

    // Get cell values
    int64_t     get_int(std::size_t column_ndx, std::size_t row_ndx) const TIGHTDB_NOEXCEPT;
    bool        get_bool(std::size_t column_ndx, std::size_t row_ndx) const TIGHTDB_NOEXCEPT;
    DateTime    get_datetime(std::size_t column_ndx, std::size_t row_ndx) const TIGHTDB_NOEXCEPT;
    float       get_float(std::size_t column_ndx, std::size_t row_ndx) const TIGHTDB_NOEXCEPT;
    double      get_double(std::size_t column_ndx, std::size_t row_ndx) const TIGHTDB_NOEXCEPT;
    StringData  get_string(std::size_t column_ndx, std::size_t row_ndx) const TIGHTDB_NOEXCEPT;
    BinaryData  get_binary(std::size_t column_ndx, std::size_t row_ndx) const TIGHTDB_NOEXCEPT;
    Mixed       get_mixed(std::size_t column_ndx, std::size_t row_ndx) const TIGHTDB_NOEXCEPT;
    DataType    get_mixed_type(std::size_t column_ndx, std::size_t row_ndx) const TIGHTDB_NOEXCEPT;
    std::size_t get_link(std::size_t column_ndx, std::size_t row_ndx) const TIGHTDB_NOEXCEPT;

    TableRef get_link_target(std::size_t column_ndx) TIGHTDB_NOEXCEPT;
    ConstTableRef get_link_target(std::size_t column_ndx) const TIGHTDB_NOEXCEPT;

    template<class T>
    typename T::RowAccessor get_link_accessor(std::size_t column_ndx, std::size_t row_ndx);

    // Set cell values
    void set_int(std::size_t column_ndx, std::size_t row_ndx, int_fast64_t value);
    void set_bool(std::size_t column_ndx, std::size_t row_ndx, bool value);
    void set_datetime(std::size_t column_ndx, std::size_t row_ndx, DateTime value);
    template<class E> void set_enum(std::size_t column_ndx, std::size_t row_ndx, E value);
    void set_float(std::size_t column_ndx, std::size_t row_ndx, float value);
    void set_double(std::size_t column_ndx, std::size_t row_ndx, double value);
    void set_string(std::size_t column_ndx, std::size_t row_ndx, StringData value);
    void set_binary(std::size_t column_ndx, std::size_t row_ndx, BinaryData value);
    void set_mixed(std::size_t column_ndx, std::size_t row_ndx, Mixed value);
    void set_link(std::size_t column_ndx, std::size_t row_ndx, std::size_t target_row_ndx);

    // Links
    bool is_null_link(std::size_t column_ndx, std::size_t row_ndx) const TIGHTDB_NOEXCEPT;
    void nullify_link(std::size_t column_ndx, std::size_t row_ndx);

    // Link lists
    LinkViewRef get_linklist(std::size_t column_ndx, std::size_t row_ndx);
    bool linklist_is_empty(std::size_t column_ndx, std::size_t row_ndx) const TIGHTDB_NOEXCEPT;
    std::size_t get_link_count(std::size_t column_ndx, std::size_t row_ndx) const TIGHTDB_NOEXCEPT;

    void add_int(std::size_t column_ndx, int64_t value);

    /// Assumes that the specified column is a subtable column (in
    /// particular, not a mixed column) and that the specified table
    /// has a spec that is compatible with that column, that is, the
    /// number of columns must be the same, and corresponding columns
    /// must have identical data types (as returned by
    /// get_column_type()).
    void insert_subtable(std::size_t col_ndx, std::size_t row_ndx, const Table*);
    void insert_mixed_subtable(std::size_t col_ndx, std::size_t row_ndx, const Table*);

    /// Like insert_subtable(std::size_t, std::size_t, const Table*)
    /// but overwrites the specified cell rather than inserting a new
    /// one.
    void set_subtable(std::size_t col_ndx, std::size_t row_ndx, const Table*);
    void set_mixed_subtable(std::size_t col_ndx, std::size_t row_ndx, const Table*);


    // Sub-tables (works on columns whose type is either 'subtable' or
    // 'mixed', for a value in a mixed column that is not a subtable,
    // get_subtable() returns null, get_subtable_size() returns zero,
    // and clear_subtable() replaces the value with an empty table.)
    TableRef get_subtable(std::size_t column_ndx, std::size_t row_ndx);
    ConstTableRef get_subtable(std::size_t column_ndx, std::size_t row_ndx) const;
    std::size_t get_subtable_size(std::size_t column_ndx, std::size_t row_ndx)
        const TIGHTDB_NOEXCEPT;
    void clear_subtable(std::size_t column_ndx, std::size_t row_ndx);

    // Backlinks
    std::size_t get_backlink_count(std::size_t row_ndx, const Table& origin,
                                   std::size_t origin_col_ndx) const TIGHTDB_NOEXCEPT;
    std::size_t get_backlink(std::size_t row_ndx, const Table& origin,
                             std::size_t origin_col_ndx, std::size_t backlink_ndx) const
        TIGHTDB_NOEXCEPT;

    //@{
    /// If this accessor is attached to a subtable, then that subtable has a
    /// parent table, and the subtable either resides in a column of type
    /// `table` or of type `mixed` in that parent. In that case
    /// get_parent_table() returns a reference to the accessor associated with
    /// the parent, and get_index_in_parent() returns the index of the row in
    /// which the subtable resides. Otherwise, if this table is a group-level
    /// table, get_parent_table() returns null and get_index_in_parent() returns
    /// the index of this table within the group. Otherwise this table is a
    /// free-standing table, get_parent_table() returns null, and
    /// get_index_in_parent() returns tightdb::npos.
    ///
    /// If this accessor is attached to a subtable, and \a column_ndx_out is
    /// specified, then `*column_ndx_out` is set to the index of the column of
    /// the parent table in which the subtable resides. If this accessor is not
    /// attached to a subtable, then `*column_ndx_out` will retain its original
    /// value upon return.
    TableRef get_parent_table(std::size_t* column_ndx_out = 0) TIGHTDB_NOEXCEPT;
    ConstTableRef get_parent_table(std::size_t* column_ndx_out = 0) const TIGHTDB_NOEXCEPT;
    std::size_t get_index_in_parent() const TIGHTDB_NOEXCEPT;
    //@}

    // Only top-level (with a group as parent) unordered tables are linkable
    bool is_linkable() const TIGHTDB_NOEXCEPT;
    Group* get_parent_group() const TIGHTDB_NOEXCEPT;

    // Aggregate functions
    std::size_t count_int(std::size_t column_ndx, int64_t value) const;
    std::size_t count_string(std::size_t column_ndx, StringData value) const;
    std::size_t count_float(std::size_t column_ndx, float value) const;
    std::size_t count_double(std::size_t column_ndx, double value) const;

    int64_t sum_int(std::size_t column_ndx) const;
    double  sum_float(std::size_t column_ndx) const;
    double  sum_double(std::size_t column_ndx) const;
    int64_t maximum_int(std::size_t column_ndx) const;
    float   maximum_float(std::size_t column_ndx) const;
    double  maximum_double(std::size_t column_ndx) const;
    int64_t minimum_int(std::size_t column_ndx) const;
    float   minimum_float(std::size_t column_ndx) const;
    double  minimum_double(std::size_t column_ndx) const;
    double  average_int(std::size_t column_ndx) const;
    double  average_float(std::size_t column_ndx) const;
    double  average_double(std::size_t column_ndx) const;

    // Searching
    std::size_t    lookup(StringData value) const;
    std::size_t    find_first_int(std::size_t column_ndx, int64_t value) const;
    std::size_t    find_first_bool(std::size_t column_ndx, bool value) const;
    std::size_t    find_first_datetime(std::size_t column_ndx, DateTime value) const;
    std::size_t    find_first_float(std::size_t column_ndx, float value) const;
    std::size_t    find_first_double(std::size_t column_ndx, double value) const;
    std::size_t    find_first_string(std::size_t column_ndx, StringData value) const;
    std::size_t    find_first_binary(std::size_t column_ndx, BinaryData value) const;

    TableView      find_all_int(std::size_t column_ndx, int64_t value);
    ConstTableView find_all_int(std::size_t column_ndx, int64_t value) const;
    TableView      find_all_bool(std::size_t column_ndx, bool value);
    ConstTableView find_all_bool(std::size_t column_ndx, bool value) const;
    TableView      find_all_datetime(std::size_t column_ndx, DateTime value);
    ConstTableView find_all_datetime(std::size_t column_ndx, DateTime value) const;
    TableView      find_all_float(std::size_t column_ndx, float value);
    ConstTableView find_all_float(std::size_t column_ndx, float value) const;
    TableView      find_all_double(std::size_t column_ndx, double value);
    ConstTableView find_all_double(std::size_t column_ndx, double value) const;
    TableView      find_all_string(std::size_t column_ndx, StringData value);
    ConstTableView find_all_string(std::size_t column_ndx, StringData value) const;
    TableView      find_all_binary(std::size_t column_ndx, BinaryData value);
    ConstTableView find_all_binary(std::size_t column_ndx, BinaryData value) const;

    TableView      get_distinct_view(std::size_t column_ndx);
    ConstTableView get_distinct_view(std::size_t column_ndx) const;

    TableView      get_sorted_view(std::size_t column_ndx, bool ascending = true);
    ConstTableView get_sorted_view(std::size_t column_ndx, bool ascending = true) const;

    TableView      get_range_view(std::size_t begin, std::size_t end);
    ConstTableView get_range_view(std::size_t begin, std::size_t end) const;

    // Pivot / aggregate operation types. Experimental! Please do not document method publicly.
    enum AggrType {
        aggr_count,
        aggr_sum,
        aggr_avg,
        aggr_min,
        aggr_max
    };

    // Simple pivot aggregate method. Experimental! Please do not document method publicly.
    void aggregate(size_t group_by_column, size_t aggr_column, AggrType op, Table& result, const Column* viewrefs = null_ptr) const;


private:
    template <class T> std::size_t find_first(std::size_t column_ndx, T value) const; // called by above methods
    template <class T> TableView find_all(size_t column_ndx, T value);
public:


    //@{
    /// Find the lower/upper bound according to a column that is
    /// already sorted in ascending order.
    ///
    /// For an integer column at index 0, and an integer value '`v`',
    /// lower_bound_int(0,v) returns the index '`l`' of the first row
    /// such that `get_int(0,l) &ge; v`, and upper_bound_int(0,v)
    /// returns the index '`u`' of the first row such that
    /// `get_int(0,u) &gt; v`. In both cases, if no such row is found,
    /// the returned value is the number of rows in the table.
    ///
    ///     3 3 3 4 4 4 5 6 7 9 9 9
    ///     ^     ^     ^     ^     ^
    ///     |     |     |     |     |
    ///     |     |     |     |      -- Lower and upper bound of 15
    ///     |     |     |     |
    ///     |     |     |      -- Lower and upper bound of 8
    ///     |     |     |
    ///     |     |      -- Upper bound of 4
    ///     |     |
    ///     |      -- Lower bound of 4
    ///     |
    ///      -- Lower and upper bound of 1
    ///
    /// These functions are similar to std::lower_bound() and
    /// std::upper_bound().
    ///
    /// The string versions assume that the column is sorted according
    /// to StringData::operator<().
    std::size_t lower_bound_int(std::size_t column_ndx, int64_t value) const TIGHTDB_NOEXCEPT;
    std::size_t upper_bound_int(std::size_t column_ndx, int64_t value) const TIGHTDB_NOEXCEPT;
    std::size_t lower_bound_bool(std::size_t column_ndx, bool value) const TIGHTDB_NOEXCEPT;
    std::size_t upper_bound_bool(std::size_t column_ndx, bool value) const TIGHTDB_NOEXCEPT;
    std::size_t lower_bound_float(std::size_t column_ndx, float value) const TIGHTDB_NOEXCEPT;
    std::size_t upper_bound_float(std::size_t column_ndx, float value) const TIGHTDB_NOEXCEPT;
    std::size_t lower_bound_double(std::size_t column_ndx, double value) const TIGHTDB_NOEXCEPT;
    std::size_t upper_bound_double(std::size_t column_ndx, double value) const TIGHTDB_NOEXCEPT;
    std::size_t lower_bound_string(std::size_t column_ndx, StringData value) const TIGHTDB_NOEXCEPT;
    std::size_t upper_bound_string(std::size_t column_ndx, StringData value) const TIGHTDB_NOEXCEPT;
    //@}

    // Queries
    // Using where(tv) is the new method to perform queries on TableView. The 'tv' can have any order; it does not
    // need to be sorted, and, resulting view retains its order. Using where.tableview(tv) is deprecated and needs 
    // 'tv' to be sorted.
    Query where(TableViewBase* tv = null_ptr) { return Query(*this, tv); }

    // FIXME: We need a ConstQuery class or runtime check against modifications in read transaction.
    Query where(TableViewBase* tv = null_ptr) const { return Query(*this, tv); }

    // Optimizing
    void optimize();

    /// Write this table (or a slice of this table) to the specified
    /// output stream.
    ///
    /// The output will have the same format as any other TightDB
    /// database file, such as those produced by Group::write(). In
    /// this case, however, the resulting database file will contain
    /// exactly one table, and that table will contain only the
    /// specified slice of the source table (this table).
    ///
    /// The new table will always have the same dynamic type (see
    /// Descriptor) as the source table (this table), and unless it is
    /// overridden (\a override_table_name), the new table will have
    /// the same name as the source table (see get_name()). Indexes
    /// (see set_index()) will not be carried over to the new table.
    ///
    /// \param offset Index of first row to include (if `size >
    /// 0`). Must be less than, or equal to size().
    ///
    /// \param size Number of rows to include. May be zero. If `size >
    /// size() - offset`, then the effective size of the written slice
    /// will be `size() - offset`.
    ///
    /// \throw std::out_of_range If `offset > size()`.
    ///
    /// FIXME: While this function does provided a maximally efficient
    /// way of serializing part of a table, it offers little in terms
    /// of general utility. This is unfortunate, because it pulls
    /// quite a large amount of code into the core library to support
    /// it.
    void write(std::ostream&, std::size_t offset = 0, std::size_t size = npos,
               StringData override_table_name = StringData()) const;

    // Conversion
    void to_json(std::ostream& out) const;
    void to_string(std::ostream& out, std::size_t limit = 500) const;
    void row_to_string(std::size_t row_ndx, std::ostream& out) const;

    // Get a reference to this table
    TableRef get_table_ref() { return TableRef(this); }
    ConstTableRef get_table_ref() const { return ConstTableRef(this); }

    /// Compare two tables for equality. Two tables are equal if, and
    /// only if, they contain the same columns and rows in the same
    /// order, that is, for each value V of type T at column index C
    /// and row index R in one of the tables, there is a value of type
    /// T at column index C and row index R in the other table that
    /// is equal to V.
    bool operator==(const Table&) const;

    /// Compare two tables for inequality. See operator==().
    bool operator!=(const Table& t) const;

    /// A subtable in a column of type 'table' (which shares descriptor with
    /// other subtables in the same column) is initially in a degenerate state
    /// where it takes up a minimal amout of space. This function returns true
    /// if, and only if the table accessor is attached to such a subtable. This
    /// function is mainly intended for debugging purposes.
    bool is_degenerate() const TIGHTDB_NOEXCEPT;

    // Debug
#ifdef TIGHTDB_DEBUG
    void Verify() const; // Must be upper case to avoid conflict with macro in ObjC
    void to_dot(std::ostream&, StringData title = StringData()) const;
    void print() const;
    MemStats stats() const;
    void dump_node_structure() const; // To std::cerr (for GDB)
    void dump_node_structure(std::ostream&, int level) const;
#else
    void Verify() const {}
#endif

    class Parent;

protected:
    /// Get a pointer to the accessor of the specified subtable. The
    /// accessor will be created if it does not already exist.
    ///
    /// The returned table pointer must **always** end up being
    /// wrapped in some instantiation of BasicTableRef<>.
    Table* get_subtable_ptr(std::size_t col_ndx, std::size_t row_ndx);

    /// See non-const get_subtable_ptr().
    const Table* get_subtable_ptr(std::size_t col_ndx, std::size_t row_ndx) const;

    /// Compare the rows of two tables under the assumption that the
    /// two tables have the same spec, and therefore the same sequence
    /// of columns.
    bool compare_rows(const Table&) const;

    void insert_into(Table* parent, std::size_t col_ndx, std::size_t row_ndx) const;

    void set_into_mixed(Table* parent, std::size_t col_ndx, std::size_t row_ndx) const;

private:
    class SliceWriter;

    // Number of rows in this table
    std::size_t m_size;

    // Underlying array structure. `m_top` is in use only for root tables; that
    // is, for tables with independent descriptor. `m_columns` contains a ref
    // for each column and search index in order of the columns. A search index
    // ref always occurs immediately after the ref of the column to which the
    // search index belongs.
    //
    // A subtable column (a column of type `type_table`) is essentially just a
    // column of 'refs' pointing to the root node of each subtable.
    //
    // To save space in the database file, a subtable in such a column always
    // starts out in a degenerate form where nothing is allocated on its behalf,
    // and a null 'ref' is stored in the corresponding slot of the column. A
    // subtable remains in this degenerate state until the first row is added to
    // the subtable.
    //
    // For this scheme to work, it must be (and is) possible to create a table
    // accessor that refers to a degenerate subtable. A table accessor (instance
    // of `Table`) refers to a degenerate subtable if, and only if `m_columns`
    // is unattached.
    //
    // FIXME: The fact that `m_columns` may be detached means that many
    // functions (even non-modifying functions) need to check for that before
    // accessing the contents of the table. This incurs a runtime
    // overhead. Consider whether this overhead can be eliminated by having
    // `Table::m_columns` always attached to something, and then detect the
    // degenerate state in a different way.
    Array m_top;
    Array m_columns; // 2nd slot in m_top (for root tables)
    Spec m_spec;     // 1st slot in m_top (for root tables)

    // Is empty when the table accessor is attached to a degenerate subtable
    // (unattached `m_columns`), otherwise it contains precisely one column
    // accessor for each column in the table, in order.
    //
    // In some cases an entry may contain null. This is currently possible only
    // in connection with Group::advance_transact(), but it means that several
    // member functions must be prepared to handle these null entries; in
    // particular, detach(), ~Table(), functions called on behalf of detach()
    // and ~Table(), and functiones called on behalf of
    // Group::advance_transact().
    //
    // FIXME: Change to `std::vector<ColumnBase*> m_col_accessors`. When this is
    // done, it is expected that it will be able to contain null-entries in
    // other cases than during Group::advance_transact(), but only after a
    // memory allocation failure.
    Array m_cols;

    mutable std::size_t m_ref_count;
    mutable const StringIndex* m_search_index;

    // If this table is a root table (has independent descriptor),
    // then Table::m_descriptor refers to the accessor of its
    // descriptor when, and only when the descriptor accessor
    // exists. This is used to ensure that at most one descriptor
    // accessor exists for each underlying descriptor at any given
    // point in time. Subdescriptors are kept unique by means of a
    // registry in the parent descriptor. Table::m_descriptor is
    // always null for tables with shared descriptor.
    mutable Descriptor* m_descriptor;

    // Table view instances
    mutable std::vector<const TableViewBase*> m_views;

    typedef std::vector<RowBase*> row_accessors;
    mutable row_accessors m_row_accessors;

#ifdef TIGHTDB_ENABLE_REPLICATION
    // Used only in connection with
    // SharedGroup::advance_read_transact().
    bool m_dirty;
#endif

    /// Disable copying assignment.
    ///
    /// It could easily be implemented by calling assign(), but the
    /// non-checking nature of the low-level dynamically typed API
    /// makes it too risky to offer this feature as an
    /// operator.
    ///
    /// FIXME: assign() has not yet been implemented, but the
    /// intention is that it will copy the rows of the argument table
    /// into this table after clearing the original contents, and for
    /// target tables without a shared spec, it would also copy the
    /// spec. For target tables with shared spec, it would be an error
    /// to pass an argument table with an incompatible spec, but
    /// assign() would not check for spec compatibility. This would
    /// make it ideal as a basis for implementing operator=() for
    /// typed tables.
    Table& operator=(const Table&);

    /// Used when constructing an accessor whose lifetime is going to be managed
    /// by reference counting. The lifetime of accessors of free-standing tables
    /// allocated on the stack by the application is not managed by reference
    /// counting, so that is a case where this tag must **not** be specified.
    class ref_count_tag {};

    /// Create an accessor for table with independent spec, and whose
    /// lifetime is managed by reference counting.
    Table(ref_count_tag, Allocator&, ref_type top_ref, Parent*, std::size_t ndx_in_parent);

    /// Create an accessor for a subtable with shared spec, and whose lifetime
    /// is managed by reference counting.
    ///
    /// It is possible to construct an accessor for a degenerate subtable by
    /// passing zero for \a columns_ref. In this case the columns will be
    /// created on demand (instantiate_before_change()).
    Table(ref_count_tag, ConstSubspecRef shared_spec, ref_type columns_ref, Parent*,
          std::size_t ndx_in_parent);

    void init_from_ref(ref_type top_ref, ArrayParent*, std::size_t ndx_in_parent);
    void init_from_ref(ConstSubspecRef shared_spec, ref_type columns_ref,
                       ArrayParent* parent, std::size_t ndx_in_parent);

    // view management support:
    void from_view_remove(std::size_t row_ndx, TableViewBase* view); // FIXME: Please rename to remove_by_view()

    void do_remove(std::size_t row_ndx);

    static void do_insert_column(Descriptor&, std::size_t column_ndx, DataType type,
                                 StringData name, Table* link_target_table);
    static void do_remove_column(Descriptor&, std::size_t column_ndx);
    static void do_rename_column(Descriptor&, std::size_t column_ndx, StringData name);

    struct InsertSubtableColumns;
    struct RemoveSubtableColumns;

    void insert_root_column(std::size_t column_ndx, ColumnType, StringData name,
                            Table* link_target_table);
    void remove_root_column(std::size_t column_ndx);

    struct SubtableUpdater {
        virtual void update(const ColumnTable&, std::size_t row_ndx, Array& subcolumns) = 0;
        virtual void update_accessor(Table&, std::size_t row_ndx) = 0;
        virtual ~SubtableUpdater() {}
    };
    static void update_subtables(Descriptor&, SubtableUpdater*);
    void update_subtables(const std::size_t* col_path_begin, const std::size_t* col_path_end,
                          SubtableUpdater*);

    struct AccessorUpdater {
        virtual void update(Table&) = 0;
        virtual void update_parent(Table&) = 0;
        virtual ~AccessorUpdater() {}
    };
    void update_accessors(const std::size_t* col_path_begin, const std::size_t* col_path_end,
                          AccessorUpdater&);

    void create_columns();
    ColumnBase* create_column_accessor(ColumnType, std::size_t col_ndx, std::size_t ndx_in_parent);
    void create_column_accessors();
    void destroy_column_accessors() TIGHTDB_NOEXCEPT;

    /// Called in the context of Group::commit() to ensure that
    /// attached table accessors stay valid across a commit. Please
    /// note that this works only for non-transactional commits. Table
    /// accessors obtained during a transaction are always detached
    /// when the transaction ends.
    void update_from_parent(std::size_t old_baseline) TIGHTDB_NOEXCEPT;

    /// After a column has been inserted or removed, this function is called to
    /// allow column accessors at higer column indexes to update cached
    /// information pertaining to their index within the table.
    ///
    /// \column_ndx_begin The index of the first column whose accessor may need
    /// to be updated.
    ///
    /// \param ndx_in_parent_diff The change in the index of a column within
    /// Table::m_columns. Note that this may differ from the change in logical
    /// column index due to the presence of search indexes in Table::m_columns.
    void adjust_column_index(std::size_t column_ndx_begin, int ndx_in_parent_diff)
        TIGHTDB_NOEXCEPT;

    void set_index(std::size_t column_ndx, bool update_spec);

    // Support function for conversions
    void to_json_row(std::size_t row_ndx, std::ostream& out) const;
    void to_string_header(std::ostream& out, std::vector<std::size_t>& widths) const;
    void to_string_row(std::size_t row_ndx, std::ostream& out,
                       const std::vector<std::size_t>& widths) const;

    // Detach accessor from underlying table. Caller must ensure that
    // a reference count exists upon return, for example by obtaining
    // an extra reference count before the call.
    //
    // This function puts this table accessor into the detached
    // state. This detaches it from the underlying structure of array
    // nodes. It also recursively detaches accessors for subtables,
    // and the type descriptor accessor. When this function returns,
    // is_attached() will return false.
    //
    // This function may be called for a table accessor that is
    // already in the detached state (idempotency).
    //
    // It is also valid to call this function for a table accessor
    // that has not yet been detached, but whose underlying structure
    // of arrays have changed in an unpredictable/unknown way. This
    // kind of change generally happens when a modifying table
    // operation fails, and also when one transaction is ended and a
    // new one is started.
    void detach() TIGHTDB_NOEXCEPT;

    /// Detach and remove all attached subtable accessors.
    void discard_subtable_accessors() TIGHTDB_NOEXCEPT;

    // Detach the type descriptor accessor if it exists.
    void discard_desc_accessor() TIGHTDB_NOEXCEPT;

    void bind_ref() const TIGHTDB_NOEXCEPT { ++m_ref_count; }
    void unbind_ref() const TIGHTDB_NOEXCEPT { if (--m_ref_count == 0) delete this; }

    void register_view(const TableViewBase* view);
    void unregister_view(const TableViewBase* view) TIGHTDB_NOEXCEPT;
    void detach_views_except(const TableViewBase* view) TIGHTDB_NOEXCEPT;

    void register_row_accessor(RowBase*) const;
    void unregister_row_accessor(RowBase*) const TIGHTDB_NOEXCEPT;
    void discard_row_accessors() TIGHTDB_NOEXCEPT;

    class UnbindGuard;

    ColumnType get_real_column_type(std::size_t column_ndx) const TIGHTDB_NOEXCEPT;

    const Array* get_column_root(std::size_t col_ndx) const TIGHTDB_NOEXCEPT;
    std::pair<const Array*, const Array*> get_string_column_roots(std::size_t col_ndx) const
        TIGHTDB_NOEXCEPT;

    const ColumnBase& get_column_base(std::size_t column_ndx) const TIGHTDB_NOEXCEPT;
    ColumnBase& get_column_base(std::size_t column_ndx);
    ColumnLinkBase& get_column_linkbase(size_t ndx);
    template <class T, ColumnType col_type> T& get_column(std::size_t ndx);
    template <class T, ColumnType col_type> const T& get_column(std::size_t ndx) const TIGHTDB_NOEXCEPT;
    Column& get_column(std::size_t column_ndx);
    const Column& get_column(std::size_t column_ndx) const TIGHTDB_NOEXCEPT;
    ColumnFloat& get_column_float(std::size_t column_ndx);
    const ColumnFloat& get_column_float(std::size_t column_ndx) const TIGHTDB_NOEXCEPT;
    ColumnDouble& get_column_double(std::size_t column_ndx);
    const ColumnDouble& get_column_double(std::size_t column_ndx) const TIGHTDB_NOEXCEPT;
    AdaptiveStringColumn& get_column_string(std::size_t column_ndx);
    const AdaptiveStringColumn& get_column_string(std::size_t column_ndx) const TIGHTDB_NOEXCEPT;
    ColumnBinary& get_column_binary(std::size_t column_ndx);
    const ColumnBinary& get_column_binary(std::size_t column_ndx) const TIGHTDB_NOEXCEPT;
    ColumnStringEnum& get_column_string_enum(std::size_t column_ndx);
    const ColumnStringEnum& get_column_string_enum(std::size_t column_ndx) const TIGHTDB_NOEXCEPT;
    ColumnTable& get_column_table(std::size_t column_ndx);
    const ColumnTable& get_column_table(std::size_t column_ndx) const TIGHTDB_NOEXCEPT;
    ColumnMixed& get_column_mixed(std::size_t column_ndx);
    const ColumnMixed& get_column_mixed(std::size_t column_ndx) const TIGHTDB_NOEXCEPT;

    void instantiate_before_change();
    void validate_column_type(const ColumnBase& column, ColumnType expected_type, std::size_t ndx) const;

    static std::size_t get_size_from_ref(ref_type top_ref, Allocator&) TIGHTDB_NOEXCEPT;
    static std::size_t get_size_from_ref(ref_type spec_ref, ref_type columns_ref,
                                         Allocator&) TIGHTDB_NOEXCEPT;

    /// Create an empty table with independent spec and return just
    /// the reference to the underlying memory.
    static ref_type create_empty_table(Allocator&);

    /// Create a column of the specified type, fill it with the
    /// specified number of default values, and return just the
    /// reference to the underlying memory.
    static ref_type create_column(ColumnType column_type, size_t num_default_values, Allocator&);

    /// Construct a copy of the columns array of this table using the
    /// specified allocator and return just the ref to that array.
    ///
    /// In the clone, no string column will be of the enumeration
    /// type.
    ref_type clone_columns(Allocator&) const;

    /// Construct a complete copy of this table (including its spec)
    /// using the specified allocator and return just the ref to the
    /// new top array.
    ref_type clone(Allocator&) const;

    /// True for `type_Link` and `type_LinkList`.
    static bool is_link_type(DataType) TIGHTDB_NOEXCEPT;

    void initialize_link_targets(Group&, std::size_t table_ndx);
    void create_backlinks_column(Table& origin, std::size_t origin_col_ndx,
                                 ColumnType origin_col_type);
    ColumnBackLink& get_backlink_column(std::size_t origin_table_ndx,
                                        std::size_t origin_col_ndx) TIGHTDB_NOEXCEPT;
    void update_backlink_column_ref(std::size_t origin_table_ndx, std::size_t old_col_ndx,
                                    std::size_t new_col_ndx);

    // Precondition: 1 <= end - begin
    std::size_t* record_subtable_path(std::size_t* begin,
                                      std::size_t* end) const TIGHTDB_NOEXCEPT;

    /// Get a pointer to the accessor of the specified subtable if the
    /// accessor exists, otherwise this function return null.
    Table* get_subtable_accessor(std::size_t col_ndx, std::size_t row_ndx) TIGHTDB_NOEXCEPT;

    void discard_subtable_accessor(std::size_t col_ndx, std::size_t row_ndx) TIGHTDB_NOEXCEPT;

    void adj_row_acc_insert_rows(std::size_t row_ndx, std::size_t num_rows) TIGHTDB_NOEXCEPT;
    void adj_subtab_acc_insert_rows(std::size_t row_ndx, std::size_t num_rows) TIGHTDB_NOEXCEPT;
    void adj_row_acc_erase_row(std::size_t row_ndx) TIGHTDB_NOEXCEPT;
    void adj_subtab_acc_erase_row(std::size_t row_ndx) TIGHTDB_NOEXCEPT;
    void adj_row_acc_move_last_over(std::size_t target_row_ndx, std::size_t last_row_ndx)
        TIGHTDB_NOEXCEPT;
    void adj_subtab_acc_move_last_over(std::size_t target_row_ndx, std::size_t last_row_ndx)
        TIGHTDB_NOEXCEPT;
    void adj_clear_nonroot() TIGHTDB_NOEXCEPT;
    void adj_insert_column(std::size_t col_ndx);
    void adj_erase_column(std::size_t col_ndx) TIGHTDB_NOEXCEPT;

    void mark_dirty() TIGHTDB_NOEXCEPT;
    void recursive_mark_dirty() TIGHTDB_NOEXCEPT;

#ifdef TIGHTDB_ENABLE_REPLICATION
    Replication* get_repl() TIGHTDB_NOEXCEPT;
#endif

    /// Refresh the dirty part of the accessor subtree rooted at this table
    /// accessor.
    ///
    /// The following conditions are necessary and sufficient for the proper
    /// operation of this function:
    ///
    ///  - The parent accessor is in a valid state (already refreshed). If the
    ///    parent is a group, then the group accessor (excluding its table
    ///    accessors) must be in a valid state. If the parent is a table, then
    ///    the table accessor (excluding its subtable accessors) must be in a
    ///    valid state.
    ///
    ///  - Every table accessor in the subtree (this one, or one of its
    ///    descendants) is marked dirty if it needs to be refreshed, or if it
    ///    has a descendant accessor that needs to be refreshed.
    ///
    ///  - This table accessor, as well as all its descendant accessors, are in
    ///    a collective state that satisfies the Accessor Hierarchy
    ///    Correspondence Guarantee with respect to the root ref stored in the
    ///    parent.
    ///
    ///  - If this table has shared descriptor, then the `index in parent`
    ///    property of the contained spec accessor is valid.
    void refresh_accessor_tree(std::size_t ndx_in_parent);

    void set_ndx_in_parent(std::size_t ndx_in_parent) TIGHTDB_NOEXCEPT;

#ifdef TIGHTDB_DEBUG
    void to_dot_internal(std::ostream&) const;
#endif

    friend class SubtableNode;
    friend class _impl::TableFriend;
    friend class Query;
    template<class> friend class util::bind_ptr;
    friend class LangBindHelper;
    friend class TableViewBase;
    friend class TableView;
    template<class T> friend class Columns;
    friend class ParentNode;
    template<class> friend class SequentialGetter;
    friend class RowBase;
};


inline void Table::remove(std::size_t row_ndx)
{
    detach_views_except(0);
    do_remove(row_ndx);
}

inline void Table::from_view_remove(std::size_t row_ndx, TableViewBase* view)
{
    detach_views_except(view);
    do_remove(row_ndx);
}

inline void Table::remove_last()
{
    if (!is_empty())
        remove(size()-1);
}

inline void Table::register_view(const TableViewBase* view)
{
    m_views.push_back(view);
}



class Table::Parent: public ArrayParent {
public:
    ~Parent() TIGHTDB_NOEXCEPT TIGHTDB_OVERRIDE {}

protected:
    virtual StringData get_child_name(std::size_t child_ndx) const TIGHTDB_NOEXCEPT;

    /// Returns true if this parent is a Group (i.e. the child is a
    /// top-level table.
    virtual bool is_parent_group() const TIGHTDB_NOEXCEPT {return false;}

    /// If this table parent is a column of some parent table, then this
    /// function must return the pointer to the parent table, otherwise it must
    /// return null.
    ///
    /// If \a column_ndx_out is not null, this function must assign the index of
    /// the column within the parent table to `*column_ndx_out` when , and only
    /// when this table parent is a column in a parent table.
    virtual Table* get_parent_table(std::size_t* column_ndx_out = 0) const TIGHTDB_NOEXCEPT;

    /// Must be called whenever a child table accessor is about to be destroyed.
    ///
    /// Note that the argument is a pointer to the child Table rather than its
    /// `ndx_in_parent` property. This is because it must be able to operate
    /// with only the Minimal Accessor Hierarchy Consistency Guarantee.
    virtual void child_accessor_destroyed(Table* child) TIGHTDB_NOEXCEPT = 0;

    virtual std::size_t* record_subtable_path(std::size_t* begin,
                                              std::size_t* end) TIGHTDB_NOEXCEPT;

    friend class Table;
};





// Implementation:

inline bool Table::is_attached() const TIGHTDB_NOEXCEPT
{
    // Note that it is not possible to tie the state of attachment of a table to
    // the state of attachment of m_top, because tables with shared spec do not
    // have a 'top' array. Neither is it possible to tie it to the state of
    // attachment of m_columns, because subtables with shared spec start out in
    // a degenerate form where they do not have a 'columns' array. For these
    // reasons, it is neccessary to define the notion of attachment for a table
    // as follows: A table is attached if, and ony if m_column stores a non-null
    // parent pointer. This works because even for degenerate subtables,
    // m_columns is initialized with the correct parent pointer.
    return m_columns.has_parent();
}

inline bool Table::is_linkable() const TIGHTDB_NOEXCEPT
{
    return (get_parent_group() != NULL);
}

inline StringData Table::get_name() const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(is_attached());
    const Array& real_top = m_top.is_attached() ? m_top : m_columns;
    ArrayParent* parent = real_top.get_parent();
    if (!parent)
        return StringData();
    std::size_t index_in_parent = real_top.get_ndx_in_parent();
    TIGHTDB_ASSERT(dynamic_cast<Parent*>(parent));
    return static_cast<Parent*>(parent)->get_child_name(index_in_parent);
}

inline std::size_t Table::get_column_count() const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(is_attached());
    return m_spec.get_public_column_count();
}

inline StringData Table::get_column_name(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < get_column_count());
    return m_spec.get_column_name(ndx);
}

inline std::size_t Table::get_column_index(StringData name) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(is_attached());
    return m_spec.get_column_index(name);
}

inline ColumnType Table::get_real_column_type(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < m_spec.get_column_count());
    return m_spec.get_column_type(ndx);
}

inline DataType Table::get_column_type(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(ndx < m_spec.get_column_count());
    return m_spec.get_public_column_type(ndx);
}

template <class C, ColumnType coltype>
C& Table::get_column(std::size_t ndx)
{
    ColumnBase& column = get_column_base(ndx);
#ifdef TIGHTDB_DEBUG
    validate_column_type(column, coltype, ndx);
#endif
    return static_cast<C&>(column);
}

template <class C, ColumnType coltype>
const C& Table::get_column(std::size_t ndx) const TIGHTDB_NOEXCEPT
{
    const ColumnBase& column = get_column_base(ndx);
#ifdef TIGHTDB_DEBUG
    validate_column_type(column, coltype, ndx);
#endif
    return static_cast<const C&>(column);
}

inline bool Table::has_shared_type() const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(is_attached());
    return !m_top.is_attached();
}


class Table::UnbindGuard {
public:
    UnbindGuard(Table* table) TIGHTDB_NOEXCEPT: m_table(table)
    {
    }

    ~UnbindGuard() TIGHTDB_NOEXCEPT
    {
        if (m_table)
            m_table->unbind_ref();
    }

    Table& operator*() const TIGHTDB_NOEXCEPT
    {
        return *m_table;
    }

    Table* operator->() const TIGHTDB_NOEXCEPT
    {
        return m_table;
    }

    Table* get() const TIGHTDB_NOEXCEPT
    {
        return m_table;
    }

    Table* release() TIGHTDB_NOEXCEPT
    {
        Table* table = m_table;
        m_table = 0;
        return table;
    }

private:
    Table* m_table;
};


inline Table::Table(Allocator& alloc):
    m_size(0), m_top(alloc), m_columns(alloc), m_spec(alloc), m_ref_count(1), m_search_index(0),
    m_descriptor(0)
{
#ifdef TIGHTDB_ENABLE_REPLICATION
    m_dirty = false;
#endif
    ref_type ref = create_empty_table(alloc); // Throws
    init_from_ref(ref, null_ptr, 0);
}

inline Table::Table(const Table& t, Allocator& alloc):
    m_size(0), m_top(alloc), m_columns(alloc), m_spec(alloc), m_ref_count(1), m_search_index(0),
    m_descriptor(0)
{
#ifdef TIGHTDB_ENABLE_REPLICATION
    m_dirty = false;
#endif
    ref_type ref = t.clone(alloc); // Throws
    init_from_ref(ref, null_ptr, 0);
}

inline Table::Table(ref_count_tag, Allocator& alloc, ref_type top_ref,
                    Parent* parent, std::size_t ndx_in_parent):
    m_size(0), m_top(alloc), m_columns(alloc), m_spec(alloc), m_ref_count(0), m_search_index(0),
    m_descriptor(0)
{
#ifdef TIGHTDB_ENABLE_REPLICATION
    m_dirty = false;
#endif
    init_from_ref(top_ref, parent, ndx_in_parent);
}

inline Table::Table(ref_count_tag, ConstSubspecRef shared_spec, ref_type columns_ref,
                    Parent* parent, std::size_t ndx_in_parent):
    m_size(0), m_top(shared_spec.get_alloc()), m_columns(shared_spec.get_alloc()),
    m_spec(shared_spec.get_alloc()), m_ref_count(0), m_search_index(0), m_descriptor(0)
{
#ifdef TIGHTDB_ENABLE_REPLICATION
    m_dirty = false;
#endif
    init_from_ref(shared_spec, columns_ref, parent, ndx_in_parent);
}


inline void Table::set_index(std::size_t column_ndx)
{
    set_index(column_ndx, true);
}

inline TableRef Table::create(Allocator& alloc)
{
    ref_type ref = create_empty_table(alloc); // Throws
    Table* table = new Table(ref_count_tag(), alloc, ref, null_ptr, 0); // Throws
    return table->get_table_ref();
}

inline TableRef Table::copy(Allocator& alloc) const
{
    ref_type ref = clone(alloc); // Throws
    Table* table = new Table(ref_count_tag(), alloc, ref, null_ptr, 0); // Throws
    return table->get_table_ref();
}

template<class T> inline Columns<T> Table::column(std::size_t column)
{
    return Columns<T>(column, this);
}

inline bool Table::is_empty() const TIGHTDB_NOEXCEPT
{
    return m_size == 0;
}

inline std::size_t Table::size() const TIGHTDB_NOEXCEPT
{
    return m_size;
}

inline Table::RowExpr Table::get(std::size_t row_ndx) TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(row_ndx < size());
    return RowExpr(this, row_ndx);
}

inline Table::ConstRowExpr Table::get(std::size_t row_ndx) const TIGHTDB_NOEXCEPT
{
    TIGHTDB_ASSERT(row_ndx < size());
    return ConstRowExpr(this, row_ndx);
}

inline Table::RowExpr Table::front() TIGHTDB_NOEXCEPT
{
    return get(0);
}

inline Table::ConstRowExpr Table::front() const TIGHTDB_NOEXCEPT
{
    return get(0);
}

inline Table::RowExpr Table::back() TIGHTDB_NOEXCEPT
{
    return get(m_size-1);
}

inline Table::ConstRowExpr Table::back() const TIGHTDB_NOEXCEPT
{
    return get(m_size-1);
}

inline Table::RowExpr Table::operator[](std::size_t row_ndx) TIGHTDB_NOEXCEPT
{
    return get(row_ndx);
}

inline Table::ConstRowExpr Table::operator[](std::size_t row_ndx) const TIGHTDB_NOEXCEPT
{
    return get(row_ndx);
}

inline std::size_t Table::add_empty_row(std::size_t num_rows)
{
    std::size_t row_ndx = m_size;
    insert_empty_row(row_ndx, num_rows); // Throws
    return row_ndx; // Return index of first new row
}

inline const Table* Table::get_subtable_ptr(std::size_t col_ndx, std::size_t row_ndx) const
{
    return const_cast<Table*>(this)->get_subtable_ptr(col_ndx, row_ndx); // Throws
}

inline void Table::insert_bool(std::size_t column_ndx, std::size_t row_ndx, bool value)
{
    insert_int(column_ndx, row_ndx, value);
}

inline void Table::insert_datetime(std::size_t column_ndx, std::size_t row_ndx, DateTime value)
{
    insert_int(column_ndx, row_ndx, value.get_datetime());
}

template<class E>
inline void Table::insert_enum(std::size_t column_ndx, std::size_t row_ndx, E value)
{
    insert_int(column_ndx, row_ndx, value);
}

inline void Table::insert_subtable(std::size_t col_ndx, std::size_t row_ndx)
{
    insert_subtable(col_ndx, row_ndx, 0); // Null stands for an empty table
}

inline ConstTableRef Table::get_link_target(std::size_t col_ndx) const TIGHTDB_NOEXCEPT
{
    return const_cast<Table*>(this)->get_link_target(col_ndx);
}

template<class E>
inline void Table::set_enum(std::size_t column_ndx, std::size_t row_ndx, E value)
{
    set_int(column_ndx, row_ndx, value);
}

inline TableRef Table::get_subtable(std::size_t column_ndx, std::size_t row_ndx)
{
    return TableRef(get_subtable_ptr(column_ndx, row_ndx));
}

inline ConstTableRef Table::get_subtable(std::size_t column_ndx, std::size_t row_ndx) const
{
    return ConstTableRef(get_subtable_ptr(column_ndx, row_ndx));
}

inline ConstTableRef Table::get_parent_table(std::size_t* column_ndx_out) const TIGHTDB_NOEXCEPT
{
    return const_cast<Table*>(this)->get_parent_table(column_ndx_out);
}

inline bool Table::operator==(const Table& t) const
{
    return m_spec == t.m_spec && compare_rows(t); // Throws
}

inline bool Table::operator!=(const Table& t) const
{
    return !(*this == t); // Throws
}

inline bool Table::is_degenerate() const TIGHTDB_NOEXCEPT
{
    return !m_columns.is_attached();
}

inline void Table::insert_into(Table* parent, std::size_t col_ndx, std::size_t row_ndx) const
{
    parent->insert_subtable(col_ndx, row_ndx, this);
}

inline void Table::set_into_mixed(Table* parent, std::size_t col_ndx, std::size_t row_ndx) const
{
    parent->insert_mixed_subtable(col_ndx, row_ndx, this);
}

inline std::size_t Table::get_size_from_ref(ref_type top_ref, Allocator& alloc) TIGHTDB_NOEXCEPT
{
    const char* top_header = alloc.translate(top_ref);
    std::pair<int_least64_t, int_least64_t> p = Array::get_two(top_header, 0);
    ref_type spec_ref = to_ref(p.first), columns_ref = to_ref(p.second);
    return get_size_from_ref(spec_ref, columns_ref, alloc);
}

inline bool Table::is_link_type(DataType type) TIGHTDB_NOEXCEPT
{
    return type == type_Link || type == type_LinkList;
}

inline void Table::update_backlink_column_ref(std::size_t origin_table_ndx,
                                              std::size_t old_column_ndx,
                                              std::size_t new_column_ndx)
{
    m_spec.update_backlink_column_ref(origin_table_ndx, old_column_ndx, new_column_ndx);
}

inline std::size_t* Table::record_subtable_path(std::size_t* begin,
                                                std::size_t* end) const TIGHTDB_NOEXCEPT
{
    const Array& real_top = m_top.is_attached() ? m_top : m_columns;
    std::size_t index_in_parent = real_top.get_ndx_in_parent();
    TIGHTDB_ASSERT(begin < end);
    *begin++ = index_in_parent;
    ArrayParent* parent = real_top.get_parent();
    TIGHTDB_ASSERT(parent);
    TIGHTDB_ASSERT(dynamic_cast<Parent*>(parent));
    return static_cast<Parent*>(parent)->record_subtable_path(begin, end);
}

inline std::size_t* Table::Parent::record_subtable_path(std::size_t* begin,
                                                        std::size_t*) TIGHTDB_NOEXCEPT
{
    return begin;
}

template<class T>
typename T::RowAccessor Table::get_link_accessor(std::size_t column_ndx, std::size_t row_ndx)
{
    size_t row_pos_in_target = get_link(column_ndx, row_ndx);
    TableRef target_table = get_link_target(column_ndx);

    Table* table = &*target_table;
    T* typed_table = (T*)(table);
    return (*typed_table)[row_pos_in_target];
}

inline void Table::mark_dirty() TIGHTDB_NOEXCEPT
{
#ifdef TIGHTDB_ENABLE_REPLICATION
    m_dirty = true;
#endif
}

#ifdef TIGHTDB_ENABLE_REPLICATION
inline Replication* Table::get_repl() TIGHTDB_NOEXCEPT
{
    return m_top.get_alloc().get_replication();
}
#endif

inline void Table::set_ndx_in_parent(std::size_t ndx_in_parent) TIGHTDB_NOEXCEPT
{
    if (m_top.is_attached()) {
        // Root table (independent descriptor)
        m_top.set_ndx_in_parent(ndx_in_parent);
    }
    else {
        // Subtable with shared descriptor
        m_columns.set_ndx_in_parent(ndx_in_parent);
    }
}


// The purpose of this class is to give internal access to some, but
// not all of the non-public parts of the Table class.
class _impl::TableFriend {
public:
    typedef Table::UnbindGuard UnbindGuard;

    static ref_type create_empty_table(Allocator& alloc)
    {
        return Table::create_empty_table(alloc); // Throws
    }

    static ref_type clone(const Table& table, Allocator& alloc)
    {
        return table.clone(alloc); // Throws
    }

    static ref_type clone_columns(const Table& table, Allocator& alloc)
    {
        return table.clone_columns(alloc); // Throws
    }

    static Table* create_ref_counted(Allocator& alloc, ref_type top_ref,
                                     Table::Parent* parent, std::size_t ndx_in_parent)
    {
        return new Table(Table::ref_count_tag(), alloc, top_ref, parent, ndx_in_parent); // Throws
    }

    static Table* create_ref_counted(ConstSubspecRef shared_spec, ref_type columns_ref,
                                     Table::Parent* parent, std::size_t ndx_in_parent)
    {
        return new Table(Table::ref_count_tag(), shared_spec, columns_ref,
                         parent, ndx_in_parent); // Throws
    }

    static void set_top_parent(Table& table, ArrayParent* parent,
                               std::size_t ndx_in_parent) TIGHTDB_NOEXCEPT
    {
        table.m_top.set_parent(parent, ndx_in_parent);
    }

    static void update_from_parent(Table& table, std::size_t old_baseline) TIGHTDB_NOEXCEPT
    {
        table.update_from_parent(old_baseline);
    }

    static void detach(Table& table) TIGHTDB_NOEXCEPT
    {
        table.detach();
    }

    static void discard_row_accessors(Table& table) TIGHTDB_NOEXCEPT
    {
        table.discard_row_accessors();
    }

    static void discard_subtable_accessors(Table& table) TIGHTDB_NOEXCEPT
    {
        table.discard_subtable_accessors();
    }

    static void discard_subtable_accessor(Table& table, std::size_t col_ndx, std::size_t row_ndx)
        TIGHTDB_NOEXCEPT
    {
        table.discard_subtable_accessor(col_ndx, row_ndx);
    }

    static void bind_ref(Table& table) TIGHTDB_NOEXCEPT
    {
        table.bind_ref();
    }

    static void unbind_ref(Table& table) TIGHTDB_NOEXCEPT
    {
        table.unbind_ref();
    }

    static bool compare_rows(const Table& a, const Table& b)
    {
        return a.compare_rows(b); // Throws
    }

    static std::size_t get_size_from_ref(ref_type ref, Allocator& alloc) TIGHTDB_NOEXCEPT
    {
        return Table::get_size_from_ref(ref, alloc);
    }

    static std::size_t get_size_from_ref(ref_type spec_ref, ref_type columns_ref,
                                         Allocator& alloc) TIGHTDB_NOEXCEPT
    {
        return Table::get_size_from_ref(spec_ref, columns_ref, alloc);
    }

    static Spec* get_spec(Table& table) TIGHTDB_NOEXCEPT
    {
        return &table.m_spec;
    }

    static const Spec* get_spec(const Table& table) TIGHTDB_NOEXCEPT
    {
        return &table.m_spec;
    }

    static std::size_t* record_subtable_path(const Table& table, std::size_t* begin,
                                             std::size_t* end) TIGHTDB_NOEXCEPT
    {
        return table.record_subtable_path(begin, end);
    }

    static void insert_column(Descriptor& desc, std::size_t column_ndx, DataType type,
                              StringData name, Table* link_target_table)
    {
        Table::do_insert_column(desc, column_ndx, type, name, link_target_table); // Throws
    }

    static void remove_column(Descriptor& desc, std::size_t column_ndx)
    {
        Table::do_remove_column(desc, column_ndx); // Throws
    }

    static void rename_column(Descriptor& desc, std::size_t column_ndx, StringData name)
    {
        Table::do_rename_column(desc, column_ndx, name); // Throws
    }

    static void clear_root_table_desc(const Table& root_table) TIGHTDB_NOEXCEPT
    {
        TIGHTDB_ASSERT(!root_table.has_shared_type());
        root_table.m_descriptor = 0;
    }

    static Table* get_subtable_accessor(Table& table, std::size_t col_ndx,
                                        std::size_t row_ndx) TIGHTDB_NOEXCEPT
    {
        return table.get_subtable_accessor(col_ndx, row_ndx);
    }

    static void adj_accessors_insert_rows(Table& table, std::size_t row_ndx,
                                          std::size_t num_rows) TIGHTDB_NOEXCEPT
    {
        table.adj_row_acc_insert_rows(row_ndx, num_rows);
        table.adj_subtab_acc_insert_rows(row_ndx, num_rows);
    }

    static void adj_accessors_erase_row(Table& table, std::size_t row_ndx) TIGHTDB_NOEXCEPT
    {
        table.adj_row_acc_erase_row(row_ndx);
        table.adj_subtab_acc_erase_row(row_ndx);
    }

    static void adj_accessors_move_last_over(Table& table, std::size_t target_row_ndx,
                                             std::size_t last_row_ndx) TIGHTDB_NOEXCEPT
    {
        table.adj_row_acc_move_last_over(target_row_ndx, last_row_ndx);
        table.adj_subtab_acc_move_last_over(target_row_ndx, last_row_ndx);
    }

    static void adj_clear_nonroot(Table& table) TIGHTDB_NOEXCEPT
    {
        table.adj_clear_nonroot();
    }

    static void adj_insert_column(Table& table, std::size_t col_ndx)
    {
        table.adj_insert_column(col_ndx); // Throws
    }

    static void adj_erase_column(Table& table, std::size_t col_ndx) TIGHTDB_NOEXCEPT
    {
        table.adj_erase_column(col_ndx);
    }

    static void mark_dirty(Table& table) TIGHTDB_NOEXCEPT
    {
        table.mark_dirty();
    }

    static void recursive_mark_dirty(Table& table) TIGHTDB_NOEXCEPT
    {
        table.recursive_mark_dirty();
    }

    static Descriptor* get_root_table_desc_accessor(Table& root_table) TIGHTDB_NOEXCEPT
    {
        return root_table.m_descriptor;
    }

    typedef Table::AccessorUpdater AccessorUpdater;
    static void update_accessors(Table& table, const std::size_t* col_path_begin,
                                 const std::size_t* col_path_end, AccessorUpdater& updatder)
    {
        table.update_accessors(col_path_begin, col_path_end, updatder); // Throws
    }

    static void refresh_accessor_tree(Table& table, std::size_t ndx_in_parent)
    {
        table.refresh_accessor_tree(ndx_in_parent); // Throws
    }

    static void set_ndx_in_parent(Table& table, std::size_t ndx_in_parent) TIGHTDB_NOEXCEPT
    {
        table.set_ndx_in_parent(ndx_in_parent);
    }

    static void set_shared_subspec_ndx_in_parent(Table& table, std::size_t spec_ndx_in_parent)
        TIGHTDB_NOEXCEPT
    {
        table.m_spec.set_ndx_in_parent(spec_ndx_in_parent);
    }

    static bool is_link_type(DataType type) TIGHTDB_NOEXCEPT
    {
        return Table::is_link_type(type);
    }

    static void initialize_link_targets(Table& table, Group& group, std::size_t table_ndx)
    {
        table.initialize_link_targets(group, table_ndx); // Throws
    }
};


} // namespace tightdb

#endif // TIGHTDB_TABLE_HPP

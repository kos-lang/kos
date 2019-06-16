Table of Contents
=================
<!--ts-->
  * [Table of Contents](#table-of-contents)
  * [base](#base)
    * [all()](#all)
    * [any()](#any)
    * [array()](#array)
      * [array.prototype.fill()](#arrayprototypefill)
      * [array.prototype.indices()](#arrayprototypeindices)
      * [array.prototype.insert()](#arrayprototypeinsert)
      * [array.prototype.insert\_array()](#arrayprototypeinsert_array)
      * [array.prototype.iterator()](#arrayprototypeiterator)
      * [array.prototype.pop()](#arrayprototypepop)
      * [array.prototype.push()](#arrayprototypepush)
      * [array.prototype.repeat()](#arrayprototyperepeat)
      * [array.prototype.reserve()](#arrayprototypereserve)
      * [array.prototype.resize()](#arrayprototyperesize)
      * [array.prototype.reverse()](#arrayprototypereverse)
      * [array.prototype.size](#arrayprototypesize)
      * [array.prototype.slice()](#arrayprototypeslice)
      * [array.prototype.sort()](#arrayprototypesort)
    * [boolean()](#boolean)
    * [buffer()](#buffer)
      * [buffer.prototype.copy\_buffer()](#bufferprototypecopy_buffer)
      * [buffer.prototype.fill()](#bufferprototypefill)
      * [buffer.prototype.indices()](#bufferprototypeindices)
      * [buffer.prototype.insert()](#bufferprototypeinsert)
      * [buffer.prototype.iterator()](#bufferprototypeiterator)
      * [buffer.prototype.pack()](#bufferprototypepack)
      * [buffer.prototype.repeat()](#bufferprototyperepeat)
      * [buffer.prototype.reserve()](#bufferprototypereserve)
      * [buffer.prototype.resize()](#bufferprototyperesize)
      * [buffer.prototype.reverse()](#bufferprototypereverse)
      * [buffer.prototype.size](#bufferprototypesize)
      * [buffer.prototype.slice()](#bufferprototypeslice)
      * [buffer.prototype.unpack()](#bufferprototypeunpack)
    * [class()](#class)
      * [class.prototype.prototype](#classprototypeprototype)
    * [count()](#count)
    * [count\_elements()](#count_elements)
    * [deep()](#deep)
    * [each()](#each)
    * [enumerate()](#enumerate)
    * [eol](#eol)
    * [exception()](#exception)
      * [exception.prototype.print()](#exceptionprototypeprint)
    * [filter()](#filter)
    * [float()](#float)
    * [function()](#function)
      * [function.prototype.apply()](#functionprototypeapply)
      * [function.prototype.async()](#functionprototypeasync)
      * [function.prototype.instructions](#functionprototypeinstructions)
      * [function.prototype.line](#functionprototypeline)
      * [function.prototype.name](#functionprototypename)
      * [function.prototype.registers](#functionprototyperegisters)
      * [function.prototype.size](#functionprototypesize)
    * [generator()](#generator)
      * [generator.prototype.iterator()](#generatorprototypeiterator)
      * [generator.prototype.slice()](#generatorprototypeslice)
    * [generator\_end()](#generator_end)
    * [hex()](#hex)
    * [integer()](#integer)
      * [integer.prototype.hex()](#integerprototypehex)
    * [join()](#join)
    * [keys()](#keys)
    * [map()](#map)
    * [method()](#method)
    * [number()](#number)
    * [object()](#object)
      * [object.prototype.all()](#objectprototypeall)
      * [object.prototype.any()](#objectprototypeany)
      * [object.prototype.count()](#objectprototypecount)
      * [object.prototype.filter()](#objectprototypefilter)
      * [object.prototype.iterator()](#objectprototypeiterator)
      * [object.prototype.map()](#objectprototypemap)
      * [object.prototype.reduce()](#objectprototypereduce)
    * [print()](#print)
    * [print\_()](#print_)
    * [print\_elements()](#print_elements)
    * [print\_lines()](#print_lines)
    * [range()](#range)
    * [reduce()](#reduce)
    * [shallow()](#shallow)
    * [sort()](#sort)
    * [string()](#string)
      * [string.prototype.code()](#stringprototypecode)
      * [string.prototype.ends\_with()](#stringprototypeends_with)
      * [string.prototype.find()](#stringprototypefind)
      * [string.prototype.indices()](#stringprototypeindices)
      * [string.prototype.iterator()](#stringprototypeiterator)
      * [string.prototype.join()](#stringprototypejoin)
      * [string.prototype.ljust()](#stringprototypeljust)
      * [string.prototype.lstrip()](#stringprototypelstrip)
      * [string.prototype.repeat()](#stringprototyperepeat)
      * [string.prototype.reverse()](#stringprototypereverse)
      * [string.prototype.rfind()](#stringprototyperfind)
      * [string.prototype.rjust()](#stringprototyperjust)
      * [string.prototype.rscan()](#stringprototyperscan)
      * [string.prototype.rstrip()](#stringprototyperstrip)
      * [string.prototype.scan()](#stringprototypescan)
      * [string.prototype.size](#stringprototypesize)
      * [string.prototype.slice()](#stringprototypeslice)
      * [string.prototype.split()](#stringprototypesplit)
      * [string.prototype.split\_lines()](#stringprototypesplit_lines)
      * [string.prototype.starts\_with()](#stringprototypestarts_with)
      * [string.prototype.strip()](#stringprototypestrip)
      * [string.prototype.zfill()](#stringprototypezfill)
    * [stringify()](#stringify)
    * [sum()](#sum)
    * [thread()](#thread)
      * [thread.prototype.wait()](#threadprototypewait)
    * [whitespace](#whitespace)
    * [zip()](#zip)
  * [datetime](#datetime)
    * [now()](#now)
  * [debug](#debug)
    * [backtrace()](#backtrace)
  * [fs](#fs)
    * [is\_file()](#is_file)
    * [remove()](#remove)
  * [io](#io)
    * [append()](#append)
    * [append\_flag](#append_flag)
    * [create()](#create)
    * [create\_flag](#create_flag)
    * [file()](#file)
      * [file.prototype.close()](#fileprototypeclose)
      * [file.prototype.eof](#fileprototypeeof)
      * [file.prototype.error](#fileprototypeerror)
      * [file.prototype.position](#fileprototypeposition)
      * [file.prototype.print()](#fileprototypeprint)
      * [file.prototype.print\_()](#fileprototypeprint_)
      * [file.prototype.print\_lines()](#fileprototypeprint_lines)
      * [file.prototype.read()](#fileprototyperead)
      * [file.prototype.read\_line()](#fileprototyperead_line)
      * [file.prototype.read\_lines()](#fileprototyperead_lines)
      * [file.prototype.read\_some()](#fileprototyperead_some)
      * [file.prototype.seek()](#fileprototypeseek)
      * [file.prototype.size](#fileprototypesize)
      * [file.prototype.write()](#fileprototypewrite)
    * [open()](#open)
    * [read\_lines()](#read_lines)
    * [ro](#ro)
    * [rw](#rw)
    * [stderr](#stderr)
    * [stdin](#stdin)
    * [stdout](#stdout)
  * [iter](#iter)
    * [cycle()](#cycle)
    * [empty()](#empty)
    * [generator()](#generator)
    * [iproduct()](#iproduct)
    * [product()](#product)
    * [reverse()](#reverse)
  * [math](#math)
    * [abs()](#abs)
    * [ceil()](#ceil)
    * [e](#e)
    * [exp()](#exp)
    * [expm1()](#expm1)
    * [floor()](#floor)
    * [infinity](#infinity)
    * [is\_infinity()](#is_infinity)
    * [is\_nan()](#is_nan)
    * [nan](#nan)
    * [pi](#pi)
    * [pow()](#pow)
    * [sqrt()](#sqrt)
  * [random](#random)
    * [rand\_float()](#rand_float)
    * [rand\_integer()](#rand_integer)
    * [random()](#random)
      * [random.prototype.float()](#randomprototypefloat)
      * [random.prototype.integer()](#randomprototypeinteger)
      * [random.prototype.shuffle()](#randomprototypeshuffle)
    * [shuffle()](#shuffle)
<!--te-->
base
====

all()
-----

    all(op)
    all(op, iterable)

Determines if all elements of an iterable object fulfill a condition.

The first variant returns a function which can then be used with various
iterable objects to test their elements.

The second variant returns `true` if all elements of an iterable object,
retrieved through the `iterator()` function, test positively against
the `op` predicate and the test evaluates to a truthy value.
Otherwise returns `false`.

The function stops iterating over elements as soon as the `op` predicate
evaluates to a falsy value.

If the `op` predicate is a function, it is invoked for each element
passed as an argument and then its return value is used as the result
of the test.

If `op` is not a function, the test is performed by comparing each element
against each element using the `==` operator.

Examples:

    > all(x => x > 0, [1, 2, 3, 4])
    true
    > all(x => x > 0, [0, 1, 2, 3])
    false
    > const all_numbers = all(x => typeof x == "number")
    > all_numbers([1, 2, 3, 4])
    true
    > all_numbers([1, 2, "foo", 3])
    false

any()
-----

    any(op)
    any(op, iterable)

Determines if any elements of an iterable object fulfill a condition.

The first variant returns a function which can then be used with various
iterable objects to test their elements.

The second variant returns `true` if one or more elements of an iterable object,
retrieved through the `iterator()` function, test positively against
the `op` predicate and the test evaluates to a truthy value.
Otherwise returns `false`.

The function stops iterating over elements as soon as the `op` predicate
evaluates to a truthy value.

If the `op` predicate is a function, it is invoked for each element
passed as an argument and then its return value is used as the result
of the test.

If `op` is not a function, the test is performed by comparing each element
against each element using the `==` operator.

Examples:

    > any(x => x > 0, [0, 20, -1, -20])
    true
    > any(x => x < 0, [0, 1, 2, 3])
    false
    > const any_numbers = any(x => typeof x == "number")
    > any_numbers(["a", "b", 1])
    true
    > any_numbers(["a", "b", "c"])
    false

array()
-------

    array(size = 0, value = void)
    array(arg, ...)

Array type class.

The first variant constructs an array of the specified size.  `size` defaults
to 0.  `value` is the value to fill the array with if `size` is greater than
0.

The second variant constructs an from one or more non-numeric objects.
Each of these input arguments is converted to an array and the resulting
arrays are concatenated, producing the final array, which is returned
by the class.  The following argument types are supported:

 * array    - The array is simply appended to the new array without conversion.
              This can be used to make a copy of an array.
 * buffer   - Buffer's bytes are appended to the new array as integers.
 * function - If the function is an iterator (a primed generator), subsequent
              elements are obtained from it and appended to the array.
              For non-iterator functions an exception is thrown.
 * object   - Object's elements are extracted using shallow operation, i.e.
              without traversing its prototypes, then subsequent properties
              are appended to the array as two-element arrays containing
              the property name (key) and property value.
 * string   - All characters in the string are converted to code points (integers)
              and then each code point is subsequently appended to the new array.

The prototype of `array.prototype` is `object.prototype`.

Examples:

    > array()
    []
    > array(3, "abc")
    ["abc", "abc", "abc"]
    > array("hello")
    [104, 101, 108, 108, 111]
    > array(range(5))
    [0, 1, 2, 3, 4]
    > array({ one: 1, two: 2, three: 3 })
    [["one", 1], ["two", 2], ["three", 3]]

array.prototype.fill()
----------------------

    array.prototype.fill(value)
    array.prototype.fill(begin, value)
    array.prototype.fill(begin, end, value)

Fills specified portion of the array with a value.

Returns the array object being filled (`this`).

`value` is the object to fill the array with.

`begin` is the index at which to start filling the array.  `begin` defaults
to `void`.  `void` is equivalent to index `0`.  If `begin` is negative, it
is an offset from the end of the array.

`end` is the index at which to stop filling the array, the element at this
index will not be overwritten.  `end` defaults to `void`.  `void` is
equivalent to the size of the array.  If `end` is negative, it is an offset
from the end of the array.

Example:

    > const a = array(5)
    > a.fill("foo")
    ["foo", "foo", "foo", "foo", "foo"]

array.prototype.indices()
-------------------------

    array.prototype.indices()

A generator which produces subsequent indices of array elements.

Returns an iterator function, which yields subsequent indices of
the array's elements in ascending order.

`a.indices()` is equivalent to `range(a.size)`, where `a` is some
array.

Example:

    > [ "a", "b", "c" ].indices() -> print_elements
    0, 1, 2

array.prototype.insert()
------------------------

    array.prototype.insert(pos, iterable)
    array.prototype.insert(begin, end, iterable)

Inserts elements into an array, possibly replacing existing elements.

The array is modified in-place.

Returns the array itself (`this`).

`pos` specifies the array index at which elements are to be inserted.
If `pos` is negative, it is relative to the array's size.
If `pos` is `void`, elements are inserted at the end of the array.

`begin` and `end` specify a range of elements to be removed from the
array and replaced by the inserted elements.  A negative `begin` or
`end` is relative to the array's size.  `begin` is the index of the
first element to be removed and `end` is the index of the first element
after the removed range which will stay.  If `begin` is equal to `end`,
they are both equivalent to `pos`.

`iterable` is an iterable object on which `iterator()` function is invoked
to obtain elements to be inserted into the array.

Examples:

    > [1, 2, 3].insert(0, ["foo"])
    ["foo", 1, 2, 3]
    > [1, 2, 3].insert(-1, ["foo"])
    [1, 2, "foo", 3]
    > [1, 2, 3, 4].insert(1, 2, "foo")
    [1, "f", "o", "o", 3, 4]
    > [1, 2, 3, 4].insert(0, 3, [])
    [4]

array.prototype.insert_array()
------------------------------

    array.prototype.insert_array(pos, array)
    array.prototype.insert_array(begin, end, array)

Inserts elements from one array into `this` array, possibly replacing
existing elements.

This function is identical in behavior to `array.prototype.insert()`.  In
most circumstances `array.prototype.insert()` is recommended instead.
`array.prototype.insert_array()` requires the iterable argument to be
an array.

array.prototype.iterator()
--------------------------

    array.prototype.iterator()

A generator which produces subsequent elements of an array.

Returns an iterator function, which yields subsequent elements.

The `iterator()` function is also implicitly invoked by the `for-in` loop
statement.

Example:

    > for const v in [ 1, 2, 3 ] { print(v) }
    1
    2
    3

array.prototype.pop()
---------------------

    array.prototype.pop(num_elements = 1)

Removes elements from the end of array.

`num_elements` is the number of elements to remove and it defaults to `1`.

If `num_elements` is `1`, returns the element removed.
If `num_elements` is `0`, returns `void`.
If `num_elements` is greater than `1`, returns an array
containing the elements removed.

Throws if the array is empty or if more elements are being removed
than the array already contains.

Example:

    > [1, 2, 3, 4, 5].pop()
    5

array.prototype.push()
----------------------

    array.prototype.push(values...)

Appends every value argument to the array.

Returns the old array size before the first element was inserted.
If one or more elements are specified to insert, the returned value
is equivalent to the index of the first element inserted.

Example:

    > [1, 1, 1].push(10, 20)
    3

array.prototype.repeat()
------------------------

    array.prototype.repeat(num)

Creates a repeated array.

`num` is a non-negative number of times to repeat the array.

If `num` is a float, it is converted to integer using floor mode.

Example:

    > [7, 8, 9].repeat(4)
    [7, 8, 9, 7, 8, 9, 7, 8, 9, 7, 8, 9]

array.prototype.reserve()
-------------------------

    array.prototype.reserve(size)

Allocate array storage without resizing the array.

The function has no visible effect, but can be used for optimization
to avoid reallocating array storage when resizing it or continuously
adding more elements.

Returns the array object itself (`this`).

array.prototype.resize()
------------------------

    array.prototype.resize(size)

Resizes an array.

Returns the array being resized (`this`).

`size` is the new size of the array.

If `size` is greater than the current array size, `void` elements are
appended to expand the array.

Example:

    > const a = []
    > a.resize(5)
    [void, void, void, void, void]

array.prototype.reverse()
-------------------------

    array.prototype.reverse()

Returns a new array with elements in reverse order.

Example:

    > [1, 2, 3, 4].reverse()
    [4, 3, 2, 1]

array.prototype.size
--------------------

    array.prototype.size

Read-only size of the array (integer).

Example:

    > [1, 10, 100].size
    3

array.prototype.slice()
-----------------------

    array.prototype.slice(begin, end)

Extracts a range of elements from an array.

Returns a new array.

It can be used to create a flat copy of an array.

`begin` and `end` specify the range of elements to extract in a new
array.  `begin` is the index of the first element and `end` is the index
of the element trailing the last element to extract.
A negative index is an offset from the end, such that `-1` indicates the
last element of the array.
If `begin` is `void`, it is equivalent to `0`.  If `end` is `void`, it is
equivalent to array size.

This function is invoked by the slice operator.

Examples:

    > [1, 2, 3, 4, 5, 6, 7, 8].slice(0, 4)
    [1, 2, 3, 4]
    > [1, 2, 3, 4, 5, 6, 7, 8].slice(void, void)
    [1, 2, 3, 4, 5, 6, 7, 8]
    > [1, 2, 3, 4, 5, 6, 7, 8].slice(-5, -1)
    [4, 5, 6, 7]

array.prototype.sort()
----------------------

    array.prototype.sort(key=void, reverse=false)
    array.prototype.sort(reverse)

Sorts array in-place.

Returns the array being sorted (`this`).

Uses a stable sorting algorithm, which preserves order of elements for
which sorting keys compare as equal.

`key` is a single-argument function which produces a sorting key for each
element of the array.  The array elements are then sorted by the keys using
the '<' operator.  By default `key` is `void` and the elements themselves
are used as sorting keys.

`reverse` defaults to `false`.  If `reverse` is specified as `true`,
the array elements are sorted in reverse order, i.e. in a descending key
order.

Example:

    > [8, 5, 6, 0, 10, 2].sort()
    [0, 2, 5, 6, 8, 10]

boolean()
---------

    boolean(value = false)

Boolean type class.

Returns the value converted to a boolean using standard truth detection
rules.

If `value` is `false`, `void`, integer `0` or float `0.0` returns `false`.
Otherwise returns `true`.

If `value` is not provided, returns `false`.

The prototype of `boolean.prototype` is `object.prototype`.

Examples:

    > boolean()
    false
    > boolean(0)
    false
    > boolean([])
    true
    > boolean("")
    true
    > boolean("false")
    true

buffer()
--------

    buffer(size = 0, value = 0)
    buffer(arg, ...)

Buffer type class.

The first variant constructs a buffer of the specified size.  `size` defaults
to 0.  `value` is the value to fill the buffer with is `size` is greater than
0.  `value` must be a number from 0 to 255 (floor operation is applied to
floats), it defaults to 0 if it's not specified.

The second variant constructs a buffer from one or more non-numeric objects.
Each of these input arguments is converted to a buffer and the resulting
buffers are concatenated, producing the final buffer, which is returned
by the class.  The following argument types are supported:

 * array    - The array must contain numbers from 0 to 255 (floor operation
              is applied to floats).  Any other array elements trigger an
              exception.  The array is converted to a buffer containing
              bytes with values from the array.
 * buffer   - A buffer is simply concatenated with other input arguments without
              any transformation.
              This can be used to make a copy of a buffer.
 * function - If the function is an iterator (a primed generator), subsequent
              elements are obtained from it and added to the buffer.  The
              values returned by the iterator must be numbers from 0 to 255
              (floor operation is applied to floats), any other values trigger
              an exception.
              For non-iterator functions an exception is thrown.
 * string   - The string is converted to an UTF-8 representation stored
              into a buffer.

The prototype of `buffer.prototype` is `object.prototype`.

Examples:

    > buffer()
    <>
    > buffer(5)
    <00 00 00 00 00>
    > buffer("hello")
    <68 65 6c 6c 6f>
    > buffer(range(4))
    <00 01 02 03>

buffer.prototype.copy_buffer()
------------------------------

    buffer.prototype.copy_buffer(src_buf)
    buffer.prototype.copy_buffer(src_buf, src_begin)
    buffer.prototype.copy_buffer(src_buf, src_begin, src_end)
    buffer.prototype.copy_buffer(dst_begin, src_buf)
    buffer.prototype.copy_buffer(dst_begin, src_buf, src_begin)
    buffer.prototype.copy_buffer(dst_begin, src_buf, src_begin, src_end)

Copies a range of bytes from source buffer to a buffer.

Returns the destination buffer being modified (`this`).

Stops copying once the last byte in the destination buffer is overwritten,
the destination buffer is not grown even if more bytes from the source
buffer could be copied.

`dst_begin` is the position at which to start placing bytes from the source
buffer.  `dst_begin` defaults to `0`.  If it is `void`, it is equivalent
to `0`.  If it is negative, it is an offset from the end of the destination
buffer.

`src_buf` is the source buffer to copy from.

`src_begin` is the offset of the first byte in the source buffer to start
copying from.  `src_begin` defaults to `0`.  If it is `void`, it is
equivalent to `0`.  If it is negative, it is an offset from the end of
the source buffer.

`src_end` is the offset of the byte at which to stop copying from the
source buffer.  This byte is not copied.  `src_end` defaults to the size
of the source buffer.  If it is `void`, it is equivalent to the size
of the source buffer.  If it is negative, it is an offset from the end
of the source buffer.

Example:

    > const dst = buffer([1, 1, 1, 1, 1])
    > const src = buffer([2, 2, 2, 2, 2])
    > dst.copy_buffer(2, src)
    <01 01 02 02 02>

buffer.prototype.fill()
-----------------------

    buffer.prototype.fill(value)
    buffer.prototype.fill(begin, value)
    buffer.prototype.fill(begin, end, value)

Fills specified portion of the buffer with a value.

Returns the buffer object being filled (`this`).

`value` is the byte value to fill the buffer with.  It must be a number from
`0` to `255`, inclusive.  Float numbers are rounded using floor mode.

`begin` is the index at which to start filling the buffer.  `begin` defaults
to `void`.  `void` is equivalent to index `0`.  If `begin` is negative, it
is an offset from the end of the buffer.

`end` is the index at which to stop filling the buffer, the element at this
index will not be overwritten.  `end` defaults to `void`.  `void` is
equivalent to the size of the buffer.  If `end` is negative, it is an offset
from the end of the buffer.

Example:

    > const b = buffer(5)
    > b.fill(0x20)
    <20 20 20 20 20>

buffer.prototype.indices()
--------------------------

    buffer.prototype.indices()

A generator which produces subsequent indices of buffer elements.

Returns an iterator function, which yields subsequent indices of
the buffer's elements in ascending order.

`b.indices()` is equivalent to `range(b.size)`, where `b` is some
buffer.

Example:

    > buffer(4).indices() -> print_elements
    0, 1, 2, 3

buffer.prototype.insert()
-------------------------

    buffer.prototype.insert(pos, obj)
    buffer.prototype.insert(begin, end, obj)

Inserts elements into a buffer, possibly replacing existing elements.

The buffer is modified in-place.

Returns the buffer itself (`this`).

`pos` specifies the buffer index at which elements are to be inserted.
If `pos` is negative, it is relative to the buffer's size.
If `pos` is `void`, elements are inserted at the end of the buffer.

`begin` and `end` specify a range of elements to be removed from the
buffer and replaced by the inserted elements.  A negative `begin` or
`end` is relative to the buffer's size.  `begin` is the index of the
first element to be removed and `end` is the index of the first element
after the removed range which will stay.  If `begin` is equal to `end`,
they are both equivalent to `pos`.

`iterable` is an iterable object on which `iterator()` function is invoked
to obtain elements to be inserted into the buffer.  The elements to be
inserted, which are yielded by the iterator, must be numbers from 0 to 255.

buffer.prototype.iterator()
---------------------------

    buffer.prototype.iterator()

A generator which produces subsequent elements of a buffer.

Returns an iterator function, which yields integers, which are
subsequent elements of the buffer.

The `iterator()` function is also implicitly invoked by the `for-in` loop
statement.

Examples:

    > buffer([1, 2, 3]).iterator() -> print_elements
    1, 2, 3
    > for const v in buffer([10, 11, 12]) { print(v) }
    10
    11
    12

buffer.prototype.pack()
-----------------------

    buffer.prototype.pack(format, args...)

Convert parameters to binary form and appends them to a buffer.

Returns the buffer which has been modified.

`format` is a string, which describes how values are to be packed.

TODO - refine format

buffer.prototype.repeat()
-------------------------

    buffer.prototype.repeat(num)

Creates a repeated buffer.

`num` is a non-negative number of times to repeat the buffer.

If `num` is a float, it is converted to integer using floor mode.

Example:

    > buffer([7, 8, 9]).repeat(4)
    <07, 08, 09, 07, 08, 09, 07, 08, 09, 07, 08, 09>

buffer.prototype.reserve()
--------------------------

    buffer.prototype.reserve(size)

Allocate buffer storage without resizing the buffer.

The function has no visible effect, but can be used for optimization
to avoid reallocating buffer storage when resizing it.

Returns the buffer object itself (`this`).

buffer.prototype.resize()
-------------------------

    buffer.prototype.resize(size)

Resizes a buffer.

Returns the buffer being resized (`this`).

`size` is the new size of the buffer.

If `size` is greater than the current buffer size, `0` elements are
appended to expand the buffer.

Example:

    > const a = buffer()
    > b.resize(5)
    <00 00 00 00 00>

buffer.prototype.reverse()
--------------------------

    buffer.prototype.reverse()

Returns a new buffer with elements in reverse order.

Example:

    > buffer([10, 20, 30]).reverse()
    <30, 20, 10>

buffer.prototype.size
---------------------

    buffer.prototype.size

Read-only size of the buffer (integer).

Example:

    > buffer([1, 10, 100]).size
    3

buffer.prototype.slice()
------------------------

    buffer.prototype.slice(begin, end)

Extracts a range of elements from a buffer.

Returns a new buffer.

It can be used to create a flat copy of a buffer.

`begin` and `end` specify the range of elements to extract in a new
buffer.  `begin` is the index of the first element and `end` is the index
of the element trailing the last element to extract.
A negative index is an offset from the end, such that `-1` indicates the
last element of the buffer.
If `begin` is `void`, it is equivalent to `0`.  If `end` is `void`, it is
equivalent to buffer size.

This function is invoked by the slice operator.

Examples:

    > buffer([1, 2, 3, 4, 5, 6, 7, 8]).slice(0, 4)
    <1, 2, 3, 4>
    > buffer([1, 2, 3, 4, 5, 6, 7, 8]).slice(void, void)
    <1, 2, 3, 4, 5, 6, 7, 8>
    > buffer([1, 2, 3, 4, 5, 6, 7, 8]).slice(-5, -1)
    <4, 5, 6, 7>

buffer.prototype.unpack()
-------------------------

    buffer.prototype.unpack(pos, format)
    buffer.prototype.unpack(format)

Unpacks values from their binary form from a buffer.

Returns an array containing unpacked values.

`pos` is the position in the buffer at which to start extracting the values.
`pos` defaults to `0`.

`format` is a string, which describes how values are to be unpacked.

TODO - refine format

class()
-------

    class()

Class type class.

Because `class` is a keyword, this class can only be referenced
indirectly via the base module, it cannot be referenced if it is imported
directly into the current module.

The argument is a class object which is returned by
this class, no new object is created by it.
Throws an exception if the argument is not a class.

The prototype of `class.prototype` is `function.prototype`.

class.prototype.prototype
-------------------------

    class.prototype.prototype

Allows reading and setting prototype on class objects.

The prototype set or retrieved is the prototype used when creating
new objects of this class.

count()
-------

    count(op)
    count(op, iterable)

Counts elements of an iterable object with a predicate.

The first variant returns a function which can then be used with various
iterable objects to count their elements.

The second variant counts all elements of an iterable object, retrieved
through the `iterator()` function, using `op` to test elements.

If `op` is a function, applies it to every element and then counts for
how many elements the `op` function returns `true`.

If `op` is not a function, counts how many elements equal to `op` by
comparing each element to `op` using `==` operator.

Examples:

    > count(x => x > 0, [-1, 1, 2, -3, 4])
    3
    > const count_numbers = count(x => x instanceof number)
    > count_numbers([10, 2, "foo", 30, "bar", 4])
    4
    > count("o", "monologue")
    3

count_elements()
----------------

    count_elements(iterable)

Counts elements of an iterable object.

Returns the number of elements retrieved from the iterable object.

Elements are extracted from `iterable` object through its `iterator()`
function and counted.

Examples:

    > count_elements([1, 10, 100, 1000])
    4
    > range(2, 10, 2) -> count_elements
    4

deep()
------

    deep(obj)

A generator which produces properties of an object and all its prototypes.

Returns an iterator function, which yields 2-element arrays, which are
[key, value] pairs of subsequent properties of the `obj` object.

The order of the elements yielded is unspecified.

Example:

    > [ deep({x:0, y:1}) ... ]
    [["any", <function>], ["all", <function>], ["filter", <function>],
     ["count", <function>], ["reduce", <function>], ["iterator", <function>],
     ["map", <function>], ["y", 1], ["x", 0]]

each()
------

    each(op, iterable)
    each(op)
    each()

Applies a function to every element of an iterable object.

The first variant invokes function `op` on every element from the `iterable`
object and returns the number of elements encountered.

The second and third variant return a function which can be applied to any
iterable object.

With the third variant, the returned function will simply extract all
elements from an iterable object.  This can be useful to cycle through
all elements of an iterator.

The function returned by the second variant invokes function `op` on every
element from the `iterable` object and returns the number of elements
encountered.

The function returned by the third variant simply iterates over the
`iterable` object, but doesn't do anything with the elements.  It also
returns the number of elements encountered.

Elements are extracted from `iterable` object through its `iterator()`
function.

A typical use of the `each()` function is at the end of a chain of stream
operators `->`.

Examples:

    > range(0, 21, 10) -> each(print)
    0
    10
    20
    3
    > "abc" -> map(x => print(x + "_")) -> each()
    a_
    b_
    c_
    3

enumerate()
-----------

    enumerate(iterable)

A generator which produces indexed elements of an iterable object.

Returns an iterator function, which yields pairs (2-element arrays) containing
consecutive indices and elements from the iterable object.

Examples:

    > enumerate(["kos", "lang", "uage"]) -> print_elements
    [0, kos], [1, lang], [2, uage]
    > enumerate("lang") -> print_elements
    [0, l], [1, a], [2, n], [3, g]

eol
---

    eol

A string containing all characters considered as end of line markers by
some functions.

exception()
-----------

    exception([value])

Exception object class.

All caught exception objects have `exception.prototype` as their prototype.
This class gives access to that prototype.

Calling this class throws an exception, it does not return
an exception object.  The thrown exception's `value` property can be set
to the optional `value` argument.  In other words, calling this class
is equivalent to throwing `value`.

If `value` is not specified, `void` is thrown.

The prototype of `exception.prototype` is `object.prototype`.

exception.prototype.print()
---------------------------

    exception.prototype.print()

Prints the exception object on stdout.

filter()
--------

    filter(op)
    filter(op, iterable)

A generator which filters elements of an iterable object using the provided
function.

The first variant with one argument returns another function, which can
then be applied on any iterable object to filter it.

The second variant uses the provided function on the elements of
the specified iterable object.  It returns an iterator function, which
yields subsequent elements for which the function returns a truthy value.

`op` is the function to invoke on each element of the iterable object.

`iterable` is an object on which `iterator()` is invoked to obtain subsequent
elements of it, which are then filtered.

Examples:

    > filter(x => x < 0, [1, -2, 3, 4, -5, -6]) -> print_elements
    -2, -5, -6
    > const odd = filter(x => x & 1)
    > odd([10, 9, 8, 7, 6, 5, 4, 3, 2, 1]) -> print_elements
    9, 7, 5, 3, 1

float()
-------

    float(value = 0.0)

Float type class.

The optional `value` argument can be an integer, a float or a string.

If `value` is not provided, returns `0.0`.

If `value` is an integer, converts it to a float and returns the converted value.

If `value` is a float, returns `value`.

If `value` is a string, parses it in the same manner numeric literals are
parsed by the interpreter, assuming it is a floating-point literal.
Throws an exception if the string cannot be parsed.

The prototype of `float.prototype` is `number.prototype`.

Examples:

    > float()
    0.0
    > float(10)
    10.0
    > float("123.5")
    123.5

function()
----------

    function(func)

Function type class.

The argument is a function object which is returned by
this class, no new object is created by it.
Throws an exception if the argument is not a function.

The prototype of `function.prototype` is `object.prototype`.

function.prototype.apply()
--------------------------

    function.prototype.apply(this_object, args_array)

Invokes a function with the specified this object and arguments.

Returns the value returned by the function.

The `this_object` argument is the object which is bound to the function as
`this` for this invocation.  It can be any object or `void`.

The `args_array` argument is an array (can be empty) containing arguments for
the function.

Example:

    > fun f(a) { return this + a }
    > f.apply(1, [2])
    3

function.prototype.async()
--------------------------

    function.prototype.async(this_object, args_array)

Invokes a function asynchronously on a new thread.

Returns the created thread object.

The `this_object` argument is the object which is bound to the function as
`this` for this invocation.  It can be any object or `void`.

The `args_array` argument is an array (can be empty) containing arguments for
the function.

Example:

    > fun f(a, b) { return a + b }
    > const t = f.async(void, [1, 2])
    > t.wait()
    3

function.prototype.instructions
-------------------------------

    function.prototype.instructions

Read-only number of bytecode instructions generated for this function.

Zero, if this is a built-in function.

Example:

    > count.instructions
    26

function.prototype.line
-----------------------

    function.prototype.line

Read-only line at which the function was defined in the source code.

function.prototype.name
-----------------------

    function.prototype.name

Read-only function name.

Example:

    > count.name
    "count"

function.prototype.registers
----------------------------

    function.prototype.registers

Read-only number of registers used by the function.

Zero, if this is a built-in function.

Example:

    > count.registers
    5

function.prototype.size
-----------------------

    function.prototype.size

Read-only size of bytecode generated for this function, in bytes.

Zero, if this is a built-in function.

Example:

    > count.size
    133

generator()
-----------

    generator()

Generator function class.

The purpose of this class is to be used with the `instanceof`
operator to detect generator functions.

Calling this class throws an exception.

The prototype of `generator.prototype` is `function.prototype`.

generator.prototype.iterator()
------------------------------

    generator.prototype.iterator()

Returns the generator itself (`this`).

The `iterator()` generator is also implicitly invoked by the `for-in` loop
statement.

This allows passing an iterator from an instantiated generator to be
passed to a `for-in` loop.

Examples:

    > for const x in range(2) { print(x) }
    0
    1

generator.prototype.slice()
---------------------------

    generator.prototype.slice(begin, end)

A generator which produces a range of elements returned from an iterator
function.

Returns an iterator function, which yields the specified subset of elements
yielded by the iterator function on which it is invoked.

`begin` and `end` specify the range of elements to generate.
`begin` is the index of the first element and `end` is the index
of the element trailing the last element to extract.
A negative index is an offset from the end of the range generated by the
iterator on which this function is invoked, such that `-1` indicates the
last element, and so on.  If either `begin` or `end` is negative, the
returned value is an array instead of an iterator, which is still iterable.
If `begin` is `void`, it is equivalent to `0`.  If `end` is `void`, all
elements will be returned until the iterator is finished.

Conceptually, all `begin` elements are skipped, then elements from
`begin` index and up to, but excluding `end` index are returned.

This function is invoked by the slice operator.

Examples:

    > range(10).slice(3, 6) -> print_elements
    3, 4, 5
    > range(10).slice(7, void) -> print_elements
    7, 8, 9

generator_end()
---------------

    generator_end()

Generator end object class.

A generator end object is typically thrown when an iterator function is
called but has no more values to yield.  In other words, a thrown generator
end object indicates end of a generator.  The generator end object can
be caught and it becomes the `value` of the exception object caught.

Calling this class throws an exception.

The prototype of `generator_end.prototype` is `object.prototype`.

hex()
-----

    hex(number, min_digits = 0)

Converts an integer to a string containing hexadecimal representation of it.

`min_digits` specifies the minimum number of hexadecimal digits to print.

Examples:

    > hex(123)
    "0x7b"
    > hex(123, 4)
    "0x007b"

integer()
---------

    integer(value = 0)

Integer type class.

The optional `value` argument can be an integer, a float or a string.

If `value` is not provided, returns 0.

If `value` is an integer, returns `value`.

If `value` is a float, converts it to integer using floor mode and returns the
converted value.

If `value` is a string, parses it in the same manner numeric literals are
parsed by the interpreter, requiring that the string is an integer literal.
Throws an exception if the string is a floating-point literal or cannot be
parsed.

The prototype of `integer.prototype` is `number.prototype`.

Examples:

    > integer()
    0
    > integer(10)
    10
    > integer(4.2)
    4
    > integer("123")
    123

integer.prototype.hex()
-----------------------

    integer.prototype.hex(min_digits = 0)

Converts the integer to a string containing hexadecimal representation of it.

`min_digits` specifies the minimum number of hexadecimal digits to print.

Examples:

    > 123 .hex()
    "0x7b"
    > 123 .hex(4)
    "0x007b"

join()
------

    join(sep, iterable)
    join(sep)

Connects strings together using a separator.

The first variant returns a string, which is a concatenation of all elements
from the `iterable` object with the `sep` separator inserted in-between the
elements.

The second variant returns a function, which takes one argument `iterable`
and will return a concatenation of all elements with `sep` in-between.

`sep` must be a string.

`iterable` can return objects of any type accepted by the `string` constructor.

Elements are extracted from `iterable` object through its `iterator()`
function.

Examples:

    > join("-", ["kos", "programming", "language"])
    "kos-programming-language"
    > join(" ", "lang")
    "l a n g"
    > ["apple", "banana", "orange"] -> join(", ")
    "apple, banana, orange"
    > range(4) -> join(" ")
    "0 1 2 3"

keys()
------

    keys(iterable)

A generator which produces first elements of each element from an iterable
object.  When iterating objects (including `base.deep()` and
`base.shallow()`), object keys are yielded.

Returns an iterator function, which iterates over the elements of an iterable
object, then for each of the elements extracts its first sub-element and
yields it.

This is typically useful to iterate over keys of an object.  Normally
iterating over an object yields pairs (two-element arrays) containing
object keys and values.  The `keys` function can convert that
sequence to just keys.

Examples:

    > keys({ A: 1, B: 2, C: 3}) -> print_elements
    A, C, B
    > keys(["kos", "lang", "first"]) -> print_elements
    k, l, f

map()
-----

    map(op)
    map(op, iterable)

A generator which applies a function to each element of an iterable object.

The first variant with one argument returns another function, which can
then be applied on any iterable object to map it.

The second variant applies the provided function to the elements of
the specified iterable object.  It returns an iterator function, which
yields subsequent elements mapped through the `op` function.

`op` is the function to invoke on each element of the iterable object.

`iterable` is an object on which `iterator()` is invoked to obtain subsequent
elements of it, which are then mapped.

Examples:

    > map(x => x*10, [1, 2, 3, 4]) -> print_elements
    10, 20, 30, 40
    > const plus2 = map(x => x + 2)
    > plus2([10, 11, 12, 13]) -> print_elements
    12, 13, 14, 15

method()
--------

    method(obj, func)

Memorizes an object and a function to be called on that object.

Returns a new function, which when called, will call the specified
function `func` with the specified object `obj` passed as `this`.

`obj` is the object on which the function will be invoked.
`func` is a function which is to be applied to the object.

Example:

    > const my_array = []
    > const append = method(my_array, my_array.push)
    > append(10)
    0
    > append(20)
    1
    > my_array
    [10, 20]

number()
--------

    number(value = 0)

Numeric type class.

The optional `value` argument can be an integer, a float or a string.

If `value` is not provided, returns 0.

If `value` is an integer or a float, returns `value`.

If `value` is a string, parses it in the same manner numeric literals are
parsed by the interpreter and returns the number as either an integer or
a float, depending on the parsing result.
Throws an exception if the string cannot be parsed.

The prototype of `number.prototype` is `object.prototype`.

Examples:

    > number()
    0
    > number(10)
    10
    > number(10.0)
    10.0
    > number("123.000")
    123.0
    > number("0x100")
    256

object()
--------

    object()

Object type class.

Returns a new empty object.  Equivalent to empty object literal `{}`.

`object.prototype` is directly or indirectly the prototype for all object types.

Example:

    > object()
    {}

object.prototype.all()
----------------------

    object.prototype.all(op)

Determines if all elements of the object fulfill a condition.

Returns `true` if all elements of the object,
retrieved through the `iterator()` function, test positively against
the `op` predicate and the test evaluates to a truthy value.
Otherwise returns `false`.

The function stops iterating over elements as soon as the `op` predicate
evaluates to a falsy value.

If the `op` predicate is a function, it is invoked for each element
passed as an argument and then its return value is used as the result
of the test.

If `op` is not a function, the test is performed by comparing each element
against each element using the `==` operator.

Example:

    > [0, 1, 2, 3].all(x => x > 0)
    false

object.prototype.any()
----------------------

    object.prototype.any(op)

Determines if any elements of the object fulfill a condition.

Returns `true` if one or more elements of the object,
retrieved through the `iterator()` function, test positively against
the `op` predicate and the test evaluates to a truthy value.
Otherwise returns `false`.

The function stops iterating over elements as soon as the `op` predicate
evaluates to a truthy value.

If the `op` predicate is a function, it is invoked for each element
passed as an argument and then its return value is used as the result
of the test.

If `op` is not a function, the test is performed by comparing each element
against each element using the `==` operator.

Example:

    > [1, 2, -1, 3, 4].any(x => x < 0)
    true

object.prototype.count()
------------------------

    object.prototype.count()
    object.prototype.count(op)

Counts elements of the object with a predicate.

Counts all elements of the object, retrieved through the `iterator()`
function, using `op` to test elements.

If `op` is not specified, counts all elements retrieved through the
`iterator()` function.

If `op` is a function, applies it to every element and then counts for
how many elements the `op` function returns `true`.

If `op` is not a function, counts how many elements equal to `op` by
comparing each element to `op` using `==` operator.

Examples:

    > "monologue".count("o")
    3
    > range(3, 43, 5).count()
    8

object.prototype.filter()
-------------------------

    object.prototype.filter(op)

A generator which filters object elements.

Returns an iterator function, which yields elements of the object,
for which the `op` function return a truthy value.  The elements are
obtained through the `iterator()` function.

Example:

    > [1, 2, 3, 4, 5, 6].filter(x => x & 1) -> print_elements
    1, 3, 5

object.prototype.iterator()
---------------------------

    object.prototype.iterator()

A generator which produces properties of an object in a shallow manner,
i.e. without descending into prototypes.

Returns an iterator function, which yields 2-element arrays, which are
[key, value] pairs of subsequent properties of the object.

The order of the elements yielded is unspecified.

This is equivalent to `shallow()` function.

If the object is not of type `object`, e.g. if it is an integer,
a float, a boolean or a non-generator function, the iterator yields
the object itself.  Prototypes of array, string, buffer and function override
this generator.

Note that `void` object does not have any properties, not even `iterator()`
function, so it is not possible to iterate over it.

The `iterator()` function is also implicitly invoked by the `for-in` loop
statement.

Examples:

    > { red: 1, green: 10, blue: 100 }.iterator() -> print_elements
    ["red", 1], ["green", 10], ["blue", 100]
    > 1.5.iterator() -> print_elements
    1.5
    > true.iterator() -> print_elements
    true
    > for const k, v in { x: 10, y: -2 } { print(k, v) }
    x 10
    y -2

object.prototype.map()
----------------------

    object.prototype.map(op)

A generator which applies a function to each element of the object.

Returns an iterator function, which yields values returned from the `op`
function called on each subsequent element of the object.  Elements
are obtained through the `iterator()` function.

Example:

    > [1, 3, 5, 7].map(x => x * 2) -> print_elements
    2, 6, 10, 14

object.prototype.reduce()
-------------------------

    object.prototype.reduce(op, init)

Performs left fold operation on subsequent elements of the object,
which are obtained through the `iterator()` function.

The left fold operation is applied as follows (pseudo-code):

    const a = [e1, e2, e3, ...]
    const reduce = op(op(op(op(init, e1), e2), e3), ...)

`op` is the function which is the reduction operator.

`init` is the initial element to use for the fold.

Example:

    > [1, 3, 7, 4, 6].reduce((x, y) => x + y, 0)
    21

print()
-------

    print(values...)

Converts all arguments to printable strings and prints them on stdout.

Accepts zero or more arguments to print.

Printed values are separated with a single space.

After printing all values prints an EOL character.  If no values are
provided, just prints an EOL character.

print_()
--------

    print_(values...)

Converts all arguments to printable strings and prints them on stdout.

Accepts zero or more arguments to print.

Printed values are separated with a single space.

Unlike `print()`, does not print an EOL character after finishing printing.

print_elements()
----------------

    print_elements(iterable, separator = ", ", end = "\n")

Prints all elements from an iterable object on stdout.

Elements are extracted from `iterable` object through its `iterator()`
function, then printed using `base.print_()` function.

The optional `separator` is a string to be printed between
the items.  It defaults to a comma.

The optional `end` is a string to be printed after the last item
or if there are no items.  It defaults to a new line character.

Examples:

    > print_elements("kos")
    k, o, s
    > print_elements("kos", "\n", "")
    k
    o
    s
    > range(5) -> print_elements
    0, 1, 2, 3, 4

print_lines()
-------------

    print_lines(iterable)

Prints all elements from an iterable object on stdout on separate lines.

Elements are extracted from `iterable` object through its `iterator()`
function, then printed using `base.print()` function.

If there are no elements, nothing is printed.

Examples:

    > print_lines("kos")
    k
    o
    s
    > range(3) -> print_lines
    0
    1
    2

range()
-------

    range(stop)
    range(start, stop, step = 1)

A generator which produces an arithmetic progression of numbers.

Returns an iterator function, which yields subsequent numbers in
the specified range.

`start`, `stop` and `step` arguments are integers or floats.

`start` specifies the first number returned by the iterator.  `start`
defaults to `0`.

`step` specified the increment which is added to subsequently returned
numbers.  If `step` is greater than zero, the generated sequence is
ascending.  If `step` is negative, the generated sequence is descending.
If `step` is zero, no numbers are generated.

`stop` specifies the number ending the sequence, but not included in it.
The iterator terminates when it reaches or exceeds `stop`.

Examples:

    > range(5) -> print_elements
    0, 1, 2, 3, 4
    > range(1, 5) -> print_elements
    1, 2, 3, 4
    > range(0, 16, 4) -> print_elements
    0, 4, 8, 12
    > range(0) -> print_elements
    
    > range(2, -8, -2) -> print_elements
    2, 0, -2, -4, -6
    > for const x in range(2) { print(x) }
    0
    1

reduce()
--------

    reduce(op, init)
    reduce(op, init, iterable)

Performs left fold operation on subsequent elements of an iterable object.

The first variant returns a function which can then be directly applied
to any iterable object.

The second variant performs the application of the left fold operation as
follows:

    const a = [e1, e2, e3, ...]
    const reduce = op(op(op(op(init, e1), e2), e3), ...)

`op` is the function which is the reduction operator.

`init` is the initial element to use for the fold.

`iterable` is an object on which `iterator()` is invoked to obtain subsequent
elements of it, on which the reduction is performed.

Examples:

    > reduce((x, y) => x + y, 0, [1, 1, 1, 2, 5])
    10
    > const count_non_zero = reduce((x, y) => x + (y ? 1 : 0), 0)
    > count_non_zero([0, 0, 4, 0, 0, 5, 6, 0])
    3

shallow()
---------

    shallow(obj)

A generator which produces properties of an object in a shallow manner,
i.e. without descending into prototypes.

Returns an iterator function, which yields 2-element arrays, which are
[key, value] pairs of subsequent properties of the `obj` object.

The order of the elements yielded is unspecified.

Example:

    > [ shallow({x:0, y:1}) ... ]
    [["y", 1], ["x", 0]]

sort()
------

    sort(key = void, reverse = false)
    sort(key, reverse, iterable)
    sort(key, iterable)
    sort(reverse, iterable)
    sort(iterable)

Sorts elements from an iterable object.

Returns an array with elements extracted from the iterable object and sorted.

Uses a stable sorting algorithm, which preserves order of elements for
which sorting keys compare as equal.

The first variant returns a sort function, which can then be used
to sort any iterable object.  The usage of the returned function is
the same as the usage of the remaining variants.

The remaining variants return an array containing elements extracted
from an iterable object and sorted according to the `key` function and
`reverse` flag.  Elements are retrieved from the iterable object through
its `iterator()` function.

`key` is a single-argument function which produces a sorting key for each
element of the array.  The array elements are then sorted by the keys using
the '<' operator.  By default `key` is `void` and the elements themselves
are used as sorting keys.

`reverse` defaults to `false`.  If `reverse` is specified as `true`,
the elements are sorted in reverse order, i.e. in a descending key
order.

Examples:

    > sort("kos language") -> print_elements
     , a, a, e, g, g, k, l, n, o, s, u
    > sort(x => x[0], { foo: 1, bar: 2, baz: 3 }) -> print_elements
    [bar, 2], [baz, 3], [foo, 1]

string()
--------

    string(args...)

String type class.

Returns a new string created from converting all arguments to strings
and concatenating them.

If no arguments are provided, returns an empty string.

For multiple arguments, constructs a string which is a concatenation of
strings created from each argument.  The following argument types are
supported:

 * array    - The array must contain numbers from 0 to 0x1FFFFF, inclusive.
              Float numbers are converted to integers using floor operation.
              Any other types of array elements trigger an exception.  The
              array's elements are code points from which a new string is
              created.  The new string's length is equal to the length of
              the array.
 * buffer   - A buffer is treated as an UTF-8 sequence and it is decoded
              into a string.
 * integer  - An integer is converted to its string representation.
 * float    - An float is converted to its string representation.
 * function - If the function is an iterator (a primed generator),
              subsequent elements are obtained from it and added to the
              string.  The acceptable types of values returned from the
              iterator are: number from 0 to 0x1FFFFF inclusive, which
              is treated as a code point, array of numbers from 0 to
              0x1FFFFF, each treated as a code point, buffer treated
              as a UTF-8 sequence and string.  All elements returned
              by the iterator are concatenated in the order they are
              returned.
              If the function is not an iterator, an exception is thrown.
 * string   - No conversion is performed.

The prototype of `string.prototype` is `object.prototype`.

Examples:

    > string(10.1)
    "10.1"
    > string("kos", [108, 97, 110, 103], 32)
    "koslang32"

string.prototype.code()
-----------------------

    string.prototype.code(pos = 0)

Returns code point of a character at a given position in a string.

`pos` is the position of the character for which the code point is returned.
`pos` defaults to `0`.  If `pos` is a float, it is converted to integer
using floor method.  If `pos` is negative, it is an offset from the end of
the string.

Examples:

    > "a".code()
    97
    > "kos".code(2)
    115
    > "language".code(-2)
    103

string.prototype.ends_with()
----------------------------

    string.prototype.ends_with(str)

Determines if a string ends with `str`.

`str` is a string which is matched against the end of the current string
(`this`).

Returns `true` if the current string ends with `str` or `false` otherwise.

Examples:

    > "foobar".ends_with("bar")
    true
    > "foobar".ends_with("foo")
    false

string.prototype.find()
-----------------------

    string.prototype.find(substr, pos = 0)

Searches for a substring in a string from left to right.

Returns index of the first substring found or `-1` if the substring was not
found.

`substr` is the substring to search for.  The search is case sensitive and
an exact match must be found.

`pos` is the index in the string at which to begin the search.  It defaults
to `0`.  If it is a float, it is converted to integer using floor mode.
If it is negative, it is an offset from the end of the string.

Examples:

    > "kos".find("foo")
    -1
    > "language".find("gu")
    3
    > "language".find("g", -3)
    6

string.prototype.indices()
--------------------------

    string.prototype.indices()

A generator which produces subsequent indices of string elements.

Returns an iterator function, which yields subsequent indices of
the string's elements in ascending order.

`s.indices()` is equivalent to `range(s.size)`, where `s` is some
string.

Example:

    > "foobar".indices() -> print_elements
    0, 1, 2, 3, 4, 5

string.prototype.iterator()
---------------------------

    string.prototype.iterator()

A generator which produces subsequent elements of a string.

Returns an iterator function, which yields single-character strings
containing subsequent characters of the string.

The `iterator()` function is also implicitly invoked by the `for-in` loop
statement.

Examples:

    > "koslang".iterator() -> print_elements
    k, o, s, l, a, n, g
    > for const v in "foo" { print(v) }
    f
    o
    o

string.prototype.join()
-----------------------

    string.prototype.join(iterable)

Connects strings together using `this` string as separator.

Returns a string, which is a concatenation of all elements
from the `iterable` object with the `this` string inserted in-between the
elements.

`iterable` can return objects of any type accepted by the `string` constructor.

Elements are extracted from `iterable` object through its `iterator()`
function.

Examples:

    > "=".join(["answer", 42, "question", "?"])
    "answer=42=question=?"
    > ", ".join(["apple", "banana", "orange"])
    "apple, banana, orange"
    > range(5, 10) -> "_".join
    "5_6_7_8_9"

string.prototype.ljust()
------------------------

    string.prototype.ljust(size, fill = " ")

Left-justifies a string.

Returns a new, left-justified string of the requested size.

`size` is the length of the returned string.  If `size` is less than
the length of the current string, the current string is returned
unmodified.

`fill` is a string of length 1 containing the character to add on the right
side of the string to justify it.  The function throws an exception if
`fill` is not a string or has a different length than 1.  `fill` defaults
to a space character.

Examples:

    > "abc".ljust(5)
    "abc  "
    > "abc".ljust(7, ".")
    "abc...."
    > "abc".ljust(1)
    "abc"

string.prototype.lstrip()
-------------------------

    string.prototype.lstrip(chars = whitespace)

Removes all leading whitespace characters from a string.

Returns a new string with all whitespace characters removed from
its beginning.

`chars` is a string, which specifies the list of characters treated
as whitespace.  It defaults to `base.whitespace`.

Example:

    > "  foo  ".lstrip()
    "foo  "

string.prototype.repeat()
-------------------------

    string.prototype.repeat(num)

Creates a repeated string.

`num` is a non-negative number of times to repeat the string.

If `num` is a float, it is converted to integer using floor mode.

Examples:

    > "-".repeat(10)
    "----------"
    > "foo".repeat(5)
    "foofoofoofoofoo"

string.prototype.reverse()
--------------------------

    string.prototype.reverse()

Returns a reversed string.

Example:

    > "kos".reverse()
    "sok"

string.prototype.rfind()
------------------------

    string.prototype.rfind(substr, pos = -1)

Performs a reverse search for a substring in a string, i.e. from right to
left.

Returns index of the first substring found or `-1` if the substring was not
found.

`substr` is the substring to search for.  The search is case sensitive and
an exact match must be found.

`pos` is the index in the string at which to begin the search.  It defaults
to `-1`, which means the search by default starts from the last character of
the string.  If `pos` is a float, it is converted to integer using floor
mode.  If it is negative, it is an offset from the end of the string.

Examples:

    > "kos".rfind("foo")
    -1
    > "language".rfind("a")
    5
    > "language".find("a", 4)
    1

string.prototype.rjust()
------------------------

    string.prototype.rjust(size, fill = " ")

Right-justifies a string.

Returns a new, right-justified string of the requested size.

`size` is the length of the returned string.  If `size` is less than
the length of the current string, the current string is returned
unmodified.

`fill` is a string of length 1 containing the character to add on the left
side of the string to justify it.  The function throws an exception if
`fill` is not a string or has a different length than 1.  `fill` defaults
to a space character.

Examples:

    > "abc".rjust(5)
    "  abc"
    > "abc".rjust(7, ".")
    "....abc"
    > "abc".rjust(1)
    "abc"

string.prototype.rscan()
------------------------

    string.prototype.rscan(chars, inclusive)
    string.prototype.rscan(chars, pos = 0, inclusive = true)

Scans the string for any matching characters in reverse direction, i.e. from
right to left.

Returns the position of the first matching character found or `-1` if no
matching characters were found.

`chars` is a string containing zero or more characters to be matched.
The search starts at position `pos` and stops as soon as any character
from `chars` is found.

`pos` is the index in the string at which to begin the search.  It defaults
to `-1`, which means the search by default starts from the last character of
the string.  If `pos` is a float, it is converted to integer using floor
mode.  If it is negative, it is an offset from the end of the string.

If `inclusive` is `true` (the default), characters in `chars` are sought.
If `inclusive` is `false`, then the search stops as soon as any character
*not* in `chars` is found.

Examples:

    > "language".rscan("g")
    6
    > "language".rscan("uga", -2, false)
    2

string.prototype.rstrip()
-------------------------

    string.prototype.rstrip(chars = whitespace)

Removes all trailing whitespace characters from a string.

Returns a new string with all whitespace characters removed from
its end.

`chars` is a string, which specifies the list of characters treated
as whitespace.  It defaults to `base.whitespace`.

Example:

    > "  foo  ".rstrip()
    "  foo"

string.prototype.scan()
-----------------------

    string.prototype.scan(chars, inclusive)
    string.prototype.scan(chars, pos = 0, inclusive = true)

Scans the string for any matching characters from left to right.

Returns the position of the first matching character found or `-1` if no
matching characters were found.

`chars` is a string containing zero or more characters to be matched.
The search starts at position `pos` and stops as soon as any character
from `chars` is found.

`pos` is the index in the string at which to begin the search.  It defaults
to `0`.  If it is a float, it is converted to integer using floor mode.
If it is negative, it is an offset from the end of the string.

If `inclusive` is `true` (the default), characters in `chars` are sought.
If `inclusive` is `false`, then the search stops as soon as any character
*not* in `chars` is found.

Examples:

    > "kos".scan("")
    0
    > "kos".scan("s")
    2
    > "language".scan("uga", -5, false)
    7

string.prototype.size
---------------------

    string.prototype.size

Read-only size of the string (integer).

Example:

    > "rain\x{2601}".size
    5

string.prototype.slice()
------------------------

    string.prototype.slice(begin, end)

Extracts substring from a string.

Returns a new string, unless the entire string was selected, in which
case returns the same string object.  (Note: strings are immutable.)

`begin` and `end` specify the range of characters to extract in a new
string.  `begin` is the index of the first character and `end` is the index
of the character trailing the last character to extract.
A negative index is an offset from the end, such that `-1` indicates the
last character of the string.
If `begin` is `void`, it is equivalent to `0`.  If `end` is `void`, it is
equivalent to string size.

This function is invoked by the slice operator.

Examples:

    > "language".slice(0, 4)
    "lang"
    > "language".slice(void, void)
    "language"
    > "language".slice(-5, -1)
    "guag"

string.prototype.split()
------------------------

    string.prototype.split(sep = void, max_split = -1)

A generator which splits a string and produces subsequent parts.

Returns an iterator function, which yields subsequent parts of
the string.  The parts result from splitting the string using
the `sep` separator.

`sep` is a string which is used as a separator.  It must be non-empty.
The source string is split into parts by finding any occurences of
the separator.  The separator is not part of the resulting strings.

`sep` defaults to `void`, which indicates that a sequence of whitespaces
of any length should be used to split the string.

`max_split` indicates the maximum number of parts into which the string
is going to be split.  By default it is -1, in which case the number
of resulting parts is unlimited.

Examples:

    > "a  b    c     d".split() -> print_elements
    a, b, c, d
    > "a--b--c--d--e--f".split("--", 3) -> print_elements
    a, b, c--d--e--f

string.prototype.split_lines()
------------------------------

    string.prototype.split_lines(keep_ends = false)

A generator which produces subsequent lines extracted from a string.

Returns an iterator function, which yields strings, which are subsequent
lines, resulting from splitting the string using EOL characters from
`base.eol`.

Two subsequent EOL characters will yield an empty line, except if a CR
characters is followed by an LF character, in which case they are kept
together as one line separator.

`keep_ends` is a boolean, which indicates whether the EOL characters
should be kept at the ends of the lines.  It defaults to `false`.

Examples:

    > "line1\nline2\nline3".split_lines() -> print_elements
    line1, line2, line3

string.prototype.starts_with()
------------------------------

    string.prototype.starts_with(str)

Determines if a string begins with `str`.

`str` is a string which is matched against the beginning of the current
string (`this`).

Returns `true` if the current string begins with `str` or `false` otherwise.

Examples:

    > "foobar".starts_with("foo")
    true
    > "foobar".starts_with("bar")
    false

string.prototype.strip()
------------------------

    string.prototype.strip(chars = whitespace)

Removes all leading and trailing whitespace characters from a string.

Returns a new string with all whitespace characters removed from
its beginning and end.

`chars` is a string, which specifies the list of characters treated
as whitespace.  It defaults to `base.whitespace`.

Example:

    > "  foo  ".strip()
    "foo"

string.prototype.zfill()
------------------------

    string.prototype.zfill(size, fill = "0")

Zero-fills a string, assuming it contains a number.

Returns a new string of the requested size.  If the original string starts
with a `+` or `-` character, this character remains at the beginning of
the returned string.

`size` is the length of the returned string.  If `size` is less than
the length of the current string, the current string is returned
unmodified.

`fill` is a string of length 1 containing the character to add on the left
side of the string (after the `+` or `-` sign) to achieve the requested
length.  The function throws an exception if `fill` is not a string or has
a different length than 1.  `fill` defaults to `"0"`.

Examples:

    > "123".zfill(5)
    "00123"
    > "+123".zfill(7)
    "+000123"
    > "+abc".zfill(6, "-")
    "+--abc"

stringify()
-----------

    stringify(args...)

Converts values to human-readable string representation.

Returns a new string created from converting all arguments to strings
and concatenating them.

If no arguments are provided, returns an empty string.

`stringify()` is implicitly invoked during string interpolation, so
the result of `stringify()` is the same as the result of string
interpolation.

String arguments are treated literally without any conversion.

Integer, float, boolean and void arguments are converted to their
string representation, which is the same as in source code.

Array and object arguments are converted to a human-readable representation
similar to their apperance in source code.  Strings inside arrays
and objects are double-quoted.

Buffer arguments are converted to the form of `<xx xx ...>`, where `xx` are
two hexadecimal digits representing every byte in the buffer.

Function arguments are converted to the form of `<function nnn @ xxx>`,
where `nnn` is the function name and `xxx` is the bytecode offset of the
function's entry point.

Example:

    > stringify(true, "true", 42, [10, "str"])
    "truetrue42[10, str]"

sum()
-----

    sum(iterable, init = 0)

Sums all elements of an iterable.

Performs an addition of all elements of the `iterable` object using the `+=`
operator.  Elements are retrieved through the `iterator()` function.

`init` is an optional initial value to which all subsequent elements are
added and defaults to `0`.

The function can also be used with strings, in which case `init` must be
provided as a string.

Examples:

    > sum([10, 20, 11, 15])
    56
    > sum(["Hello", ", ", "World!"], "")
    "Hello, World!"

thread()
--------

    thread()

Thread object class.

Thread objects are created by calling `function.prototype.async()`.

The purpose of this class is to be used with the `instanceof`
operator to detect thread objects.

Calling this class directly throws an exception.

The prototype of `thread.prototype` is `object.prototype`.

thread.prototype.wait()
-----------------------

    thread.prototype.wait()

Waits for thread to complete.

Returns the return value returned from the thread function.

If the thread function ended with an exception, rethrows that exception
on the current thread.

Example:

    > fun f { return 42 }
    > const t = f.async()
    > t.wait()
    42

whitespace
----------

    whitespace

A string containing all characters considered as whitespace by some
functions.

zip()
-----

    zip(iterable0, iterable1...)

A generator which produces arrays with elements from each iterable object.

Returns an iterator function, which yields arrays.  Each array yielded has
one element from each input iterable object.

Example:

    > zip(range(4), "abcd") -> print_elements
    [0, a], [1, b], [2, c], [3, d]

datetime
========

now()
-----

    now()

Returns current time, in microseconds since the Epoch.


debug
=====

backtrace()
-----------

    backtrace()

Returns backtrace from the place of the invocation.

The backtrace returned is identical to the backtrace obtained
from an exception object.

fs
==

is_file()
---------

    is_file(pathname)

Determines whether a file exists.

Returns `true` if `pathname` exists and is a file, or `false` otherwise.

remove()
--------

    remove(pathname)

Deletes a file `pathname`.

Returns `true` if the file was successfuly deleted or `false` if
the file could not be deleted or if it did not exist in the first
place.

io
==

append()
--------

    append(filename)

Creates a new or opens an existing file in append mode, opens for reading
and writing.

Returns opened file object.

`filename` is the path to the file to create or truncate.

It is recommended to use the `file.append()` function in conjunction with
the `with` statement.

Example:

    > with const f = file.append("my.txt") { f.print("hello") }

append_flag
-----------

    append_flag

Flag used with `file.file` and `file.open`.  Indicates that the file is to
be created if it does not exist or opened at at the end for appending.
The file is always opened for reading and writing with this flag.

create()
--------

    create(filename)

Creates a new or truncates an existing file, opens for reading and writing.

Returns opened file object.

`filename` is the path to the file to create or truncate.

It is recommended to use the `file.create()` function in conjunction with
the `with` statement.

Example:

    > with const f = file.create("my.txt") { f.print("hello") }

create_flag
-----------

    create_flag

Flag used with `file.file` and `file.open`.  Indicates that the file is to
be created if it does not exist or truncated if it exists and then opened
for reading and writing.

file()
------

    file(pathname, flags = rw)

File object class.

Returns opened file object.

`pathname` is the path to the file.

`flags` is a string, which specifies file open mode compatible with
the C `fopen()` function.  It is normally recommended to use the
shorthand flag constants: `io.ro`, `io.rw` or the auxiliary
file functions `io.open()`, `io.create()` and `io.append()`
instead of specifying the flags explicitly.

It is recommended to use the `io.file` class in conjunction with
the `with` statement.

Example:

    > with const f = io.file("my.txt", io.create_flag) { f.print("hello") }

file.prototype.close()
----------------------

    file.prototype.close()

Closes the file object if it is still opened.

file.prototype.eof
------------------

    file.prototype.eof

A boolean read-only flag indicating whether the read/write pointer has
reached the end of the file object.

file.prototype.error
--------------------

    file.prototype.error

A boolean read-only flag indicating whether there was an error during the
last file operation on the file object.

file.prototype.position
-----------------------

    file.prototype.position

Read-only position of the read/write pointer in the opened file object.

file.prototype.print()
----------------------

    file.prototype.print(values...)

Converts all arguments to printable strings and writes them to the file.

Returns the file object to which the strings were written.

Accepts zero or more arguments to write.

Written values are separated with a single space.

After printing all values writes an EOL character.  If no values are
provided, just writes an EOL character.

file.prototype.print_()
-----------------------

    file.prototype.print_(values...)

Converts all arguments to printable strings and writes them to the file.

Returns the file object to which the strings were written.

Accepts zero or more arguments to write.

Written values are separated with a single space.

Unlike `file.prototype.print()`, does not write an EOL character after finishing writing.

file.prototype.print_lines()
----------------------------

    file.prototype.print_lines(iterable)

Prints all elements from an iterable object into the file on separate lines.

Elements are extracted from `iterable` object through its `iterator()`
function, then printed using `file.prototype.print()` function.

If there are no elements, nothing is printed.

file.prototype.read()
---------------------

    file.prototype.read()
    file.prototype.read(size [, buffer])

Reads bytes from an opened file object.

Returns a buffer containing the bytes read.

The first variant reads as many bytes as it can, possibly until the end
of file.

The second variant reads up to `size` bytes, or less if the file does not
have that many bytes.  It never reads more than `size` bytes.

If `buffer` is specified, bytes are appended to it and that buffer is
returned instead of creating a new buffer.

file.prototype.read_line()
--------------------------

    file.prototype.read_line(reserved_size = 4096)

Reads a single line of text from a file.

Returns the string containing the line read, including EOL character sequence.

`reserved_size` is the amount of bytes to reserve for the buffer into which
the file is read.  If the line is longer than that, the buffer will be
automatically resized.  This is an implementation detail and it may change
in the future.

This is a low-level function, `file.prototype.read_lines()` is a better choice
in most cases.

file.prototype.read_lines()
---------------------------

    file.prototype.read_lines(keep_ends = false, gran = 0x1000)

A generator which produces subsequent lines of text read from a file.

Returns an iterator function, which yields subsequent lines read
from the file on subsequent invocations.  The lines returned by the
iterator function are strings.

`keep_ends` tells whether the EOL character sequences should be kept
as part of the returned lines.  It defaults to `false`.

`gran` is the internal granularity at which the file is read.  It
defaults to 4KB.

file.prototype.read_some()
--------------------------

    file.prototype.read_some(size = 4096 [, buffer])

Reads a variable number of bytes from an opened file object.

Returns a buffer containing the bytes read.

Reads as many bytes as it can, up to the specified `size`.

`size` is the maximum bytes to read.  `size` defaults to 4096.  Less
bytes can be read if no more bytes are available.

If `buffer` is specified, bytes are appended to it and that buffer is
returned instead of creating a new buffer.

This is a low-level function, `file.prototype.read()` is a better choice
in most cases.

file.prototype.seek()
---------------------

    file.prototype.seek(pos)

Moves the read/write pointer to a different position in the file.

Returns the file object for which the pointer has been moved.

`pos` is the new, absolute position in the file where the pointer
is moved.  If it is negative, the pointer is moved relative to the end of
the file.  If it is a float, it is converted to integer using floor mode.

Throws an exception if the pointer cannot be moved for whatever reason.

file.prototype.size
-------------------

    file.prototype.size

Read-only size of the opened file object.

file.prototype.write()
----------------------

    file.prototype.write(buffer)

Writes a buffer containing bytes into an opened file object.

Returns the file object to which bytes has been written.

`buffer` is a buffer object.  Its size can be zero, in which case nothing
is written.

open()
------

    open(filename, flags = rw)

Opens a file.

Returns opened file object.

`filename` is the path to the file to open.

Optional `flags` specify open mode.  `flags` default to `rw`.

It is recommended to use the `file.open()` function in conjunction with
the `with` statement.

Example:

    > with const f = file.open("my.txt", file.create_flag) { f.print("hello") }

read_lines()
------------

    read_lines(filename, keep_ends = false, gran = 0x1000)

A generator which produces subsequent lines of text read from a file.

Returns an iterator function, which yields subsequent lines read
from the file on subsequent invocations.  The lines returned by the
iterator function are strings.

`filename` is the path to the file to open.

`keep_ends` tells whether the EOL character sequences should be kept
as part of the returned lines.  It defaults to `false`.

`gran` is the internal granularity at which the file is read.  It
defaults to 4KB.

Example:

    > for const line in file.read_lines("README.txt") { print(line) }

ro
--

    ro

Flag used with `file.file` and `file.open`.  Indicates that the file is to
be opened for reading only.

File must exist if this flag is used, or else exception is thrown.

rw
--

    rw

Flag used with `file.file` and `file.open`.  Indicates that the file is to
be opened for reading and writing.

File must exist if this flag is used, or else exception is thrown.

stderr
------

    stderr

Write-only file object corresponding to standard error.

stdin
-----

    stdin

Read-only file object corresponding to standard input.

stdout
------

    stdout

Write-only file object corresponding to standard output.

Calling `file.stdout.print()` is equivalent to `base.print()`.

iter
====

cycle()
-------

    cycle(iterable)

A generator which cycles forever over elements of an interable object.

Returns an iterator function, which yields subsequent elements of an
iterable object cyclically, infinitely.  Once yielding the last element,
it starts from the beginning and yields the first and subsequent elements
again.

`iterable` is an object on which `iterator()` is invoked to obtain subsequent
elements of it.  It is wrapped with `iter.generator()`, so that when
`iterable` is a function, the elements are cached, so they can be yielded
again.

empty()
-------

    empty()

Empty generator.

This generator, when instantiated, never yields any objects and
terminates immediately.

generator()
-----------

    generator(iterable)

Creates a generator function, which caches another generator.

Returns a generator function, which can be instantiated many times.

If `iterable` is a function, it is assumed to be a generator or an
iterator, its elements are cached, so that the returned generator
function can be instantiated multiple times.

If the returned generator is instantiated twice and processed
in parallel, the behavior is undefined.

If `iterable` is not a function, the behavior of the returned
generator upon instantiation will be equivalent to calling
`iterable.iterator()`.

iproduct()
----------

    iproduct(range_sizes...)

A generator which produces permutations of multiple 0-based ranges.

Returns an iterator function, which yields subsequent permutations
of the specified ranges.

The arguments of this function are integers, which specify how many
elements each range contains.  The number of input arguments is equal
to the number of ranges. The first range is innermost, the last range
is outermost.

`iter.iproduct(n, m)` is equivalent to `iter.product(range(n), range(m))`.

Example:

    > iter.iproduct(2, 3) -> print_elements
    [0, 0], [1, 0], [0, 1], [1, 1], [0, 2], [1, 2]
    > iter.iproduct(3, 2) -> print_elements
    [0, 0], [1, 0], [2, 0], [0, 1], [1, 1], [2, 1]

product()
---------

    product(iterables...)

A generator which produces permutations of the output of multiple other
generators.

Returns an iterator function, which yields arrays containing subsequent
permutations of values from input iterable objects.

The arguments, `iterables`, are objects on which `iterator()` is invoked
to obtain generators for each iterable object.

Example:

    > iter.product(range(5, 7), "xyz") -> print_elements
    [5, x], [6, x], [5, y], [6, y], [5, z], [6, z]

reverse()
---------

    reverse(iterable)

A generator which produces elements of an iterable object in reverse order.

For strings, arrays and buffers, returns the object of the same type
containing elements in reverse order.

For objects of other type, returns an iterator function, which yields
elements of the iterable object in backwards order.

Examples:

    > iter.reverse(range(4)) -> print_elements
    3, 2, 1, 0
    > iter.reverse("language")
    "egaugnal"

math
====

abs()
-----

    abs(number)

Returns absolute value of `number`.

Preserves the type of the input argument (integer or float).

If `number` is an integer and it is the lowest possible integer value
(`0x8000_0000_0000_0000`), then throws an exception.

Examples:

    > math.abs(-100)
    100
    > math.abs(-math.infinity)
    infinity

ceil()
------

    ceil(number)

Rounds a number to the closest, but higher or equal integer value.

Preserves the type of the input argument.  If `number` is an integer,
returns that integer.  If `number` is a float, returns a rounded float.

Examples:

    > math.ceil(10.5)
    11.0
    > math.ceil(-0.1)
    -0.0

e
-

    e

The mathematical constant e=2.7182... represented as float.

exp()
-----

    exp(number)

Returns Eulers number *e* raised to the power of `number`.

The value returned is always a float.

Examples:

    > math.exp(1)
    2.718281828459045
    > math.exp(-1)
    0.367879441171442

expm1()
-------

    expm1(number)

Returns Eulers number *e* raised to the power of `number` and subtracts `1`.

The returned value returned is always a float.

The returned value has a higher precision than `math.exp(number) - 1`.

Example:

    > math.expm1(2)
    6.38905609893065

floor()
-------

    floor(number)

Rounds a number to the closest, but lower or equal integer value.

Preserves the type of the input argument.  If `number` is an integer,
returns that integer.  If `number` is a float, returns a rounded float.

Examples:

    > math.floor(0.1)
    0.0
    > math.floor(-0.1)
    -1.0

infinity
--------

    infinity

Constant float value representing positive infinity.

is_infinity()
-------------

    is_infinity(number)

Returns `true` if the `number` is a float and its value is plus or minus
infinity, otherwise returns `false`.

Examples:

    > math.is_infinity(math.infinity)
    true
    > math.is_infinity(math.nan)
    false
    > math.is_infinity(1e60)
    false

is_nan()
--------

    is_nan(number)

Returns `true` if the `number` is a float and its value is a "not-a-number",
otherwise returns `false`.

Examples:

    > math.is_nan(math.nan)
    true
    > math.is_nan(1.0)
    false
    > math.is_nan([])
    false

nan
---

    nan

Constant float value representing "not-a-number".

pi
--

    pi

The mathematical constant π=3.1415... represented as float.

pow()
-----

    pow(num, power)

Returns `num` raised to `power`.

The returned value is always a float.

Throws an exception if `num` is negative and `power` is not an
integer value (it can still be a float type, but its value must be
mathematically an integer).

Examples:

    > math.pow(2, 2)
    4.0
    > math.pow(10, -2)
    0.01

sqrt()
------

    sqrt(number)

Returns square root of `number`.

The returned value is always a float.

Throws an exception if `number` is negative.

Example:

    > math.sqrt(4)
    2.0

random
======

rand_float()
------------

    rand_float()

Generates a pseudo-random float with uniform distribution from 0.0
(inclusive) to 1.0 (exclusive).

Returns a float in the range from 0.0 to 1.0, where 0.0 can be possibly
produced and 1.0 is never produced.

Example:

    > random.rand_float()
    0.05080192760294

rand_integer()
--------------

    rand_integer()
    rand_integer(min, max)

Generates a pseudo-random integer with uniform distribution.

Returns a random integer.

The first variant generates any integer number.

The second variant generates an integer between the chosen `min` and `max`
values.  The `min` and `max` values are included in the possible range.

Examples:

    > random.rand_integer()
    -3655836363997440814
    > random.rand_integer(-100, 100)
    42

random()
--------

    random([seed])

Pseudo-random number generator class.

Returns a new pseudo-random generator object.

If the optional argument `seed` is not specified, the random number
generator is initialized from a system-specific entropy source.  For example,
on Windows CryptGenRandom() is used, otherwise `/dev/urandom` is used if
it is available.

If `seed` is specified, it is used as seed for the pseudo-random number
generator.  `seed` is either an integer or a float.  If `seed` is a float,
it is converted to an integer using floor method.

The underlying pseudo-random generator initialized by this class
uses PCG XSH RR 32 algorithm.

The quality of pseudo-random numbers produced by this generator is sufficient
for most purposes, but it is not recommended for cryptographic applications.

Example:

    > const r = random.random(42)
    > r.integer()
    -6031299347323205752
    > r.integer()
    -474045495260715754

random.prototype.float()
------------------------

    random.prototype.float()

Generates a pseudo-random float with uniform distribution from 0.0
(inclusive) to 1.0 (exclusive).

Returns a float in the range from 0.0 to 1.0, where 0.0 can be possibly
produced and 1.0 is never produced.

Example:

    > const r = random.random(42)
    > r.float()
    0.782519239019594

random.prototype.integer()
--------------------------

    random.prototype.integer()
    random.prototype.integer(min, max)

Generates a pseudo-random integer with uniform distribution.

Returns a random integer.

The first variant generates any integer number.

The second variant generates an integer between the chosen `min` and `max`
values.  The `min` and `max` values are included in the possible range.

Examples:

    > const r = random.random(100)
    > r.integer()
    -5490786365174251167
    > r.integer(0, 1)
    0
    > r.integer(-10, 10)
    -2

random.prototype.shuffle()
--------------------------

    random.prototype.shuffle(iterable)

Generates a pseudo-random permutation of elements from an iterable object.

The pseudo-random number generator object `this` is used to generate
the permutation.

Returns an array containing a permutation of the elements extracted
from the `iterable` using its `iterator()` function.

Example:

    > const r = random.random(42)
    > range(10) -> r.shuffle
    [7, 4, 6, 5, 2, 0, 1, 3, 8, 9]

shuffle()
---------

    shuffle(iterable)

Generates a pseudo-random permutation of elements from an iterable object.

Returns an array containing a permutation of the elements extracted
from the `iterable` using its `iterator()` function.

Example:

    > range(10) -> shuffle
    [9, 4, 3, 0, 5, 7, 1, 6, 8, 2]


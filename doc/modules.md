Module lang
===========

The `lang` module supplements the Kos programming language with essential
functionality.


array
-----

TODO


array.prototype.insert
----------------------

TODO


array.prototype.insert_array
----------------------------

TODO


array.prototype.iterator
------------------------

Generator function, which yields subsequent elements of the array.

Typically this function is invoked implcitly from the `for`..`in` loop.

Example:

    const a = [ 1, 2, 3 ]
    for var elem in a {
        print("\( elem )\n")
    }

Output:

    1
    2
    3

Example:

    const a    = [ 4, 5, 6 ]
    const iter = a.iterator()
    print("\( iter() )\n")
    print("\( iter() )\n")
    print("\( iter() )\n")

Output:

    4
    5
    6


array.prototype.reserve
-----------------------

TODO


array.prototype.resize
----------------------

TODO


array.prototype.size
--------------------

TODO


array.prototype.slice
---------------------

TODO


boolean
-------

TODO


buffer
------

TODO


buffer.prototype.copy_buffer
----------------------------

TODO


buffer.prototype.fill
---------------------

TODO


buffer.prototype.insert
-----------------------

TODO


buffer.prototype.iterator
-------------------------

TODO


buffer.prototype.pack
---------------------

TODO


buffer.prototype.reserve
------------------------

TODO


buffer.prototype.resize
-----------------------

TODO


buffer.prototype.size
---------------------

TODO


buffer.prototype.slice
----------------------

TODO


buffer.prototype.unpack
-----------------------

TODO


count
-----

TODO


deep
----

TODO


filter
------

TODO


float
-----

TODO


function
--------

TODO


function.prototype.apply
------------------------

TODO


function.prototype.instructions
-------------------------------

TODO


function.prototype.iterator
---------------------------

TODO


function.prototype.name
-----------------------

TODO


function.prototype.prototype
----------------------------

TODO


function.prototype.registers
----------------------------

TODO


function.prototype.set_prototype
--------------------------------

TODO


function.prototype.size
-----------------------

TODO


integer
-------

TODO


map
---

TODO


number
------

TODO


object
------

TODO


object.prototype.iterator
-------------------------

TODO


object.prototype.count
----------------------

TODO


object.prototype.filter
-----------------------

TODO


object.prototype.map
--------------------

TODO


object.prototype.reduce
-----------------------

TODO


print
-----

TODO


range
-----

TODO


reduce
------

TODO


shallow
-------

TODO


string
------

TODO


string.prototype.iterator
-------------------------

TODO


string.prototype.size
---------------------

TODO


string.prototype.slice
----------------------

TODO


void
----

TODO


void.prototype.iterator
-----------------------

TODO


Module file
===========

The `file` module provides file manipulation facilities.


file
----

TODO


file.eof
--------

TODO


file.error
----------

TODO


file.position
-------------

TODO


file.read
---------

TODO


file.read_lines
---------------

TODO


file.read_some
--------------

TODO


file.release
------------

TODO


file.seek
---------

TODO


file.size
---------

TODO


file.write
----------

TODO


file.close
----------

TODO


is_file
-------

TODO


open
----

TODO


remove
------

TODO


stderr
------

TODO


stdin
-----

TODO


stdout
------

TODO

# Welcome to your new application

Please fill in this README file with relevant information for your application.

To learn more about using the [facil.io framework](http://facil.io), please read through the comments in the source code or the guides on the framework's website.

Good luck!

## What you're starting with

This folder contains the library code, some boilerplate application code, a complex (yet very helpful) `makefile` and some helper scripts.

### Temporary boilerplate application

The boilerplate code, which is a basic "Hello World!" HTTP application resides in the `src` folder.

If you wish to rename the folder, make sure to update the `MAIN_ROOT` in the  `makefile`.

It's also possible to use sub-folders using the `MAIN_SUBFOLDERS` variable in the `makefile` (i.e., add `foo/bar` to add the sub-folder `src/foo/bar`).

### The Library code

The facil.io library source code files reside in the `lib/facil` folder.

It's possible to remove any unused modules. For example, if you're not writing an HTTP application, it's safe to remove the `facil/http` folder and the source files it contains. If your application doesn't need Redis connectivity, it can be removed.

However, since code footprint is usually a minor concern (and can often be fixed by adding instructions to the compiler using the makefile), it is recommended that unused code be left alone in case it would be required at a later stage.


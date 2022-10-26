# Contribution Rules

Want to hack on the NVIDIA Rivermax Project ? Awesome!<br>
We only require you to sign your work, the below section describes this!<br>

## Sign Your Work
The sign-off is a simple line at the end of the explanation for the patch. Your
signature certifies that you wrote the patch or otherwise have the right to pass
it on as an open-source patch. The rules are pretty simple: if you can certify
the below (from [developercertificate.org](http://developercertificate.org/)):

We require that all contributors "Sign-Off" on their commits. This certifies
that the contribution is your original work, or you have rights to submit it
under the same license, or a compatible license.<br>
Any contribution which contains commits that are not Signed-Off will not be
accepted.<br>
To sign off on a commit you simply use the `--signoff` (or `-s`) option when committing your changes:<br>
```
$ git commit -s -m "Add cool feature."
```
This will append the following to your commit message:
```
Signed-off-by: Your Name <your@email.com>
```
By doing this you certify the following:<br>

```
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.
1 Letterman Drive
Suite D4700
San Francisco, CA, 94129

Everyone is permitted to copy and distribute verbatim copies of this license
document, but changing it is not allowed.

Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:
(a) The contribution was created in whole or in part by me and I have the right to submit it under the open source license
    indicated in the file; or
(b) The contribution is based upon previous work that, to the best of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that work with modifications, whether created in whole or in part by
    me, under the same open source license (unless I am permitted to submit under a different license), as indicated in the file;
    or
(c) The contribution was provided directly to me by some other person who certified (a), (b) or (c). and I have not modified it.
(d) I understand and agree that this project and the contribution are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is maintained indefinitely and may be redistributed consistent
    with this project or the open source license(s) involved.
```

## Important Information

### Contribution Content
The sample code should include:
* The application source code
* CMakeLists.txt
* Open source – under Apache 2.0 License
* No Rivermax headers
* No Rivermax SDK

### README.md
A README.md file must be included with the following information:
* Application description
* Indicate that the code is provided as is - demo code only for a specific functionality, not a product
* Release date
* Update date
* Version
* Tested with:
  * Rivermax version
  * OS
  * WinOF2 version / MLNX_OFED version / MLNX_EN version
  * Hardware [ConnectX-5 / ConnectX-6 Dx / BlueField-2 / ConnectX-7]
  * 3rd party components [for example: FFMPEG version etc...]
* How to run
  * Inputs parameters
  * Command line examples
* Known issues / limitations

### Pull Request
The code must be sumitted via a Pull Request.
The following information should be submitted with the Pull Request:
* How was the code tested?


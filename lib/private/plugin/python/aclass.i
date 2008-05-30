/* ------------------------------------------------------------------- */
/* Copyright (c) 2007-2008 Hanzo Archives Limited.                     */
/*                                                                     */
/* Licensed under the Apache License, Version 2.0 (the "License");     */
/* you may not use this file except in compliance with the License.    */
/* You may obtain a copy of the License at                             */
/*                                                                     */
/*     http://www.apache.org/licenses/LICENSE-2.0                      */
/*                                                                     */
/* Unless required by applicable law or agreed to in writing, software */
/* distributed under the License is distributed on an "AS IS" BASIS,   */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or     */
/* implied.                                                            */
/* See the License for the specific language governing permissions and */
/* limitations under the License.                                      */
/*                                                                     */
/* You may find more information about Hanzo Archives at               */
/*                                                                     */
/*     http://www.hanzoarchives.com/                                   */
/*                                                                     */
/* You may find more information about the WARC Tools project at       */
/*                                                                     */
/*     http://code.google.com/p/warc-tools/                            */
/* ------------------------------------------------------------------- */

/* aclass.i */
 %{
   #include <wport.h>
    /*
     * C default headers
     */

#include <stdarg.h>    /* variable function argument */
    /*
     * WARC default headers
     */

#include <wclass.x>  /* WARC base class structure */
#include <wclass.h>
     
 %}
   %varargs(const char * str = NULL,  afile_comp_t comp =2,  const char * chr = NULL) bless;
   extern void * bless   (const void *, ...);
   extern void   destroy (void *);
   extern void   cassert (void * const, const unsigned int);



    

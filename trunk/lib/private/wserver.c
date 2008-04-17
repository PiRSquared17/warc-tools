
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

/*
 * Portability header file
 */

#include <wport.h>


/*
 * WARC default headers
 */

#include <wstring.h>  /* for class's prototypes */
#include <wclass.h>   /* bless, destroy, cassert, struct Class */
#include <wsign.h>    /* CASSET macro */
#include <wserver.h>  /* for class's prototypes */
#include <wmem.h>     /* wmalloc, wfree */
#include <wmisc.h>    /* warc_bool_t ... */
#include <wendian.h>  /* warc_i32ToEndian ... */
#include <wcsafe.h>
#include <wfile.h>    /* for class's prototypes */
#include <wrecord.h>  /* for class's prototypes */
#include <wanvl.h>    /* for class's prototypes */
#include <wlist.h>    /* for class's prototypes */
#include <wfile.x>    /* WFile_getFile */
#include <wversion.h> /* the warc version */

#include <event.h>    /* event structures and functions */
#include <evhttp.h>   /* evhttp structures and functions */

/* The buffer maxumim size */
#ifndef WSERVER_MAX_BUFFER_SIZE
#define WSERVER_MAX_BUFFER_SIZE  512 
#endif /* WSERVER_MAX_BUFFER_SIZE */ 

/* The chunk maximum size (equals header a TCP tram) */
#ifndef WSERVER_MAX_CHUNK_SIZE
#define WSERVER_MAX_CHUNK_SIZE   512
#endif /* WSERVER_MAX_CHUNK_SIZE */


#define makeS(s) (s), w_strlen ((s))

struct Callbacks_arg 
{
  struct event_base * base;
  void              * server_name;
  void              * server_prefix;
  warc_bool_t         shoot_down;
};

typedef enum 
  {
    WARC_ONE_RECORD_REQUEST = 0,
    WARC_FULL_FILE_REQUEST,
    WARC_DISTANT_DUMP_REQUEST,
    WARC_DISTANT_FILTER_REQUEST
  } warc_request_t;


typedef enum
  {
    WARC_URI = 0,
    WARC_MIME,
    WARC_RTYPE
  } warc_filters_t;


/* function for matching little string in a big string */
WPRIVATE warc_bool_t match (const warc_u8_t * bigstring, warc_u32_t   bigstringlen,
                   const warc_u8_t * pattern,   warc_u32_t   patternlen)
{
  unless (bigstring)
    return (WARC_TRUE);

  if (patternlen > bigstringlen)
    return (WARC_TRUE);

  unless (w_strcasestr (bigstring, pattern) )
    return (WARC_TRUE);

  return (WARC_FALSE);
}


/**
 * @param uri: the request to analyse
 * @param pos: the position in the uri from where we start the parsing
 * 
 * @return The next field in the REST request as a WString object
 *
 * WServer REST requests fields providing function
 **/

WPRIVATE void * WServer_getUriNextItem (const warc_u8_t * uri, warc_u32_t pos)
{
  void       * item      = bless (WString, "", 0); /* the return item */
  warc_u8_t    achar [] = {' ', '\0'}; /* to append item characters */
  warc_u32_t   i;

  assert (item);
  i = pos;
  
  if (uri[i] != '/')
    {
      destroy (item);
      return (NIL);
    }
  ++ i;
  
  while ((uri [i] != '/') && (uri [i] != '\0'))
    {
      achar [0] = uri [i];
      if (WString_append (item, achar, 1))
        {
          destroy (item);
          return (NIL);
        }
      ++ i;
    }
  
  return (item);
}


/**
 * @param uri: The uri where the warc file name is given
 * @param pos: the position in the uri where the extraction begin
 *
 * @return The name of the warc file to open as a WString object
 *
 * Warc File names extraction function from the request urti
 */

WPRIVATE void * WServer_getUriFileName (const warc_u8_t * uri, 
                                        warc_u32_t pos)
{
  void       * item     = NIL;         /* the return item */
  warc_u8_t    achar [] = {' ', '\0'}; /* to append item characters */
  warc_u32_t   i;

  i = pos;

  item = bless (WString, "", 0);

/*   item = bless (WString, "", 0); */
 
  assert (item);

  if (uri [i] != '/')
    {
      destroy (item);
      return (NIL);
    }

  /* reject any suspicious request */
  if (w_strstr (WString_getText (item), ".."))
   {
      destroy (item);
      return (NIL);
   }

  while (uri [i] != '\0')
    {
      achar [0] = uri [i];
      if (WString_append (item, achar, 1))
        {
          destroy (item);
          return (NIL);
        }
      ++ i;
    }

  return (item);
}





/**
 * @param uri: the uri to parse
 * @param fname: the requested warc file name as a WString (must be blessed before calling, will be filled in the function)
 * @param comp: the compression mode
 * @param offset: the offset of the record in the warc file
 * @param what: if the we have a filter request, it indicates on what we have to filter
 * @param value: the value with which we will compare for the filter as a WString 
 * @param kind: what is requested
 * 
 * @return False if succeeds, True otherwise
 * 
 * Client Uri request Parsing function
 */

WPRIVATE warc_bool_t WServer_parseRequest (const warc_u8_t * uri, 
                                           void            * fname, 
                                           wfile_comp_t    * comp, 
                                           warc_i32_t      * offset, 
                                           warc_filters_t  * what,
                                           void            * value,
                                           warc_request_t  * kind)
{
  void       * item      = NIL;
  void       * other_item = NIL;
  warc_u32_t   pos       = 0;
  
  assert (fname);

  /* Checkin the warc file format version */
  other_item = WServer_getUriNextItem (uri, pos);
  unless (other_item)
    return (WARC_TRUE);
  
  WString_append (other_item, (const warc_u8_t *) "/", 1);
  
  pos = pos + WString_getLength (other_item);
  
  item = WServer_getUriNextItem (uri, pos);
  unless (item)
  {
    destroy (other_item);
    return (WARC_TRUE);
  }
    
  WString_concat (other_item, item);
    
  if (w_strcmp (WString_getText (other_item), (const warc_u8_t *) WARC_VERSION))
    {
      destroy (other_item);
      destroy (item);
      return (WARC_TRUE);
    }
  
  pos = pos + WString_getLength (item)+1;
  destroy (item);
  destroy (other_item);

 
  item = WServer_getUriNextItem (uri, pos);
  unless (item)
    return (WARC_TRUE);
  
  /* detecting what is required */
  unless (w_strcmp (WString_getText(item),
                    (const warc_u8_t *) "file"))
    {

      /* may be it is the full request type */
      * kind = WARC_FULL_FILE_REQUEST;

     pos = pos + WString_getLength (item)+1;
     destroy (item);

     /* recovering the compression mode */
     item = WServer_getUriNextItem (uri, pos);
     unless (item)
         return (WARC_TRUE);

     unless (w_strcmp (WString_getText(item),
                       (const warc_u8_t *) "gzip"))
         {
          *comp = WARC_FILE_COMPRESSED_GZIP;
         }
     else 
       unless (w_strcmp (WString_getText (item), 
                         (const warc_u8_t *) "uncompressed"))
         {
           * comp = WARC_FILE_UNCOMPRESSED;
         }
     else
       {
         destroy (item);
         return (WARC_TRUE);
       }

      pos    = pos + WString_getLength (item) + 1;
      destroy (item);
      
     /*recovering the record offset*/
     item = WServer_getUriNextItem (uri, pos);
     unless (item)
         return (WARC_TRUE);
  
     unless (w_check_digital_string (WString_getText (item), 
                                     WString_getLength (item)))
      {
        destroy (item);
        return (WARC_TRUE);
      }
  
     w_atou (WString_getText(item), WString_getLength (item), 
             (warc_u32_t *) offset);
     pos = pos + WString_getLength (item) + 1;
     destroy (item);
  
     item = WServer_getUriFileName (uri, pos);
     unless (item)
       return (WARC_TRUE);

     unless (item)
       return (WARC_TRUE);
      
     if (WString_setText (fname, WString_getText(item), 
                          WString_getLength (item)))
       {
         destroy (item);
         return (WARC_TRUE);
       }
      
     destroy (item);
     return (WARC_FALSE);
    }
  


  unless (w_strcmp (WString_getText(item),
                    (const warc_u8_t *) "list"))
    {

      /* may be it is the list request */

      * kind = WARC_DISTANT_DUMP_REQUEST;
      pos    = pos + WString_getLength (item) + 1;
      destroy (item);
      
     /* recovering the compression mode */
     item = WServer_getUriNextItem (uri, pos);
     unless (item)
         return (WARC_TRUE);

     unless (w_strcmp (WString_getText(item),
                       (const warc_u8_t *) "gzip"))
         {
          *comp = WARC_FILE_COMPRESSED_GZIP;
         }
     else 
       unless (w_strcmp (WString_getText (item), 
                         (const warc_u8_t *) "uncompressed"))
         {
           * comp = WARC_FILE_UNCOMPRESSED;
         }
     else
       {
         destroy (item);
         return (WARC_TRUE);
       }

     pos = pos + WString_getLength (item)+1;
     destroy (item);

     /*recovering the record offset*/
     item = WServer_getUriNextItem (uri, pos);
     unless (item)
         return (WARC_TRUE);
  
     unless (w_check_digital_string (WString_getText (item), 
                                     WString_getLength (item)))
      {
        destroy (item);
        return (WARC_TRUE);
      }
  
     w_atou (WString_getText(item), WString_getLength (item), 
             (warc_u32_t *) offset);
     pos = pos + WString_getLength (item) + 1;
     destroy (item);
  
     item = WServer_getUriFileName (uri, pos);
     unless (item)
       return (WARC_TRUE);

     unless (item)
       return (WARC_TRUE);
      
     if (WString_setText (fname, WString_getText(item), 
                          WString_getLength (item)))
       {
         destroy (item);
         return (WARC_TRUE);
       }
      
     destroy (item);
     return (WARC_FALSE);
    }


  /* may be we reclaim a record */
  unless (w_strcmp (WString_getText (item), 
                    (const warc_u8_t *) "record"))
    {
     pos    = pos + WString_getLength (item) + 1;
     destroy (item);
  
     /* detecting the compression mode */

    item = WServer_getUriNextItem (uri, pos);
     unless (w_strcmp (WString_getText (item), 
                       (const warc_u8_t *) "gzip"))
       {
         * comp = WARC_FILE_COMPRESSED_GZIP;
       }
     else 
     unless (w_strcmp (WString_getText (item), 
                      (const warc_u8_t *) "uncompressed"))
       {
        * comp = WARC_FILE_UNCOMPRESSED;
       }
    else
      {
      destroy (item);
      return (WARC_TRUE);
      }

    * kind = WARC_ONE_RECORD_REQUEST;
    pos = pos + WString_getLength (item)+1;
    destroy (item);
  
    /*recovering the record offset*/
    item = WServer_getUriNextItem (uri, pos);

    unless (item)
      return (WARC_TRUE);

  
    unless (w_check_digital_string (WString_getText(item), WString_getLength(item)))
      {
       destroy (item);
      return (WARC_TRUE);
      }

    w_atou(WString_getText(item), WString_getLength(item), (warc_u32_t *) offset);
    pos = pos + WString_getLength (item)+1;
    destroy (item);

    item = WServer_getUriFileName (uri, pos);
    unless (item)
      return (WARC_TRUE);

    if (WString_setText (fname, WString_getText(item), 
                         WString_getLength (item)))
      {
      destroy (item);
      return (WARC_TRUE);
      }
  
    destroy (item);
  
    return (WARC_FALSE);
   }

  /* may be we want to filter the file */
  unless (w_strcmp (WString_getText (item), 
                    (const warc_u8_t *) "filter"))
    {

     unless (value)
       {
       destroy (item);
       return (WARC_TRUE);
       }

     pos    = pos + WString_getLength (item) + 1;
     destroy (item);
  
     /* detecting the compression mode */

    item = WServer_getUriNextItem (uri, pos);
     unless (w_strcmp (WString_getText (item), 
                       (const warc_u8_t *) "gzip"))
       {
         * comp = WARC_FILE_COMPRESSED_GZIP;
       }
     else 
     unless (w_strcmp (WString_getText (item), 
                      (const warc_u8_t *) "uncompressed"))
       {
        * comp = WARC_FILE_UNCOMPRESSED;
       }
    else
      {
      destroy (item);
      return (WARC_TRUE);
      }

    * kind = WARC_DISTANT_FILTER_REQUEST;
    pos = pos + WString_getLength (item)+1;
    destroy (item);
  
    /*recovering the record offset*/
    item = WServer_getUriNextItem (uri, pos);

    unless (item)
      return (WARC_TRUE);

  
    unless (w_check_digital_string (WString_getText(item), WString_getLength(item)))
      {
       destroy (item);
      return (WARC_TRUE);
      }

    w_atou(WString_getText(item), WString_getLength(item), (warc_u32_t *) offset);
    pos = pos + WString_getLength (item)+1;
    destroy (item);

    item = WServer_getUriNextItem (uri, pos);

    unless (w_strcmp (WString_getText (item), 
                    (const warc_u8_t *) "uri"))
      {
      * what = WARC_URI;

      pos = pos + WString_getLength (item)+1;
      destroy (item);

      item = WServer_getUriNextItem (uri, pos);
      unless (item)
         return (WARC_TRUE);

     
      WString_setText (value, WString_getText (item),
                       WString_getLength (item));

      pos = pos + WString_getLength (item)+1;
      destroy (item);
    
      }
    else
    unless (w_strcmp (WString_getText (item), 
            (const warc_u8_t * ) "contenttype"))
      {
      * what = WARC_MIME;

      pos = pos + WString_getLength (item)+1;
      destroy (item);

      item = WServer_getUriNextItem (uri, pos);
      unless (item)
         return (WARC_TRUE);

      WString_setText (value, WString_getText (item),
                      WString_getLength (item));

      pos = pos + WString_getLength (item)+1;
      destroy (item);
      }
    else
    unless (w_strcmp (WString_getText (item), 
            (const warc_u8_t * ) "recordtype"))
      {
      * what = WARC_RTYPE;

      pos = pos + WString_getLength (item) +1;
      destroy (item);

      item = WServer_getUriNextItem (uri, pos);
      unless (item)
        return (WARC_TRUE);
      
      WString_concat (value, item);

      pos = pos + WString_getLength (item) +1;
      destroy (item);
      }
    else
      {
      destroy (item);
      return (WARC_TRUE);
      }



    item = WServer_getUriFileName (uri, pos);
    unless (item)
      return (WARC_TRUE);

    if (WString_setText (fname, WString_getText(item), 
                         WString_getLength (item)))
      {
      destroy (item);
      return (WARC_TRUE);
      }
  
    destroy (item);
  
    return (WARC_FALSE);
   }

return (WARC_TRUE);
}


/**
 * @param fname: the name of the warc file to be sent as a WString
 * @param pref: the prefix to add to the file name to be opened as a WString
 * @param comp_mode: the compression mode, only for header sending
 * @param offset: the offset in the warc file from where we start the transfert
 * @param server_name: the name of the server machine as a WString
 * @param req: the request object which will be used to send the response
 * @param buf: the buffer which will be used to send the response
 *
 * @return False if succeeds, True otherwise
 *
 * Send the response to a full warc file request
 */

WPRIVATE warc_bool_t WSend_fullResponse (void * fname, void * pref, wfile_comp_t comp_mode, warc_i32_t offset, const void * server_name, 
                                                     struct evhttp_request * req, struct evbuffer * buf)
{
   char         *   buffer         = NIL ;
   FILE         *   full_file      = NIL ;
   warc_u32_t       size           = 0   ;
   warc_u32_t       rsize          = 0   ;
   warc_u8_t        char_size[26]        ;
   warc_u8_t        content_type[25]     ;
   
   WString_concat (fname, pref);
   full_file = w_fopen_rb ((const char *) WString_getText(fname));

   unless (full_file)
      {
      evbuffer_add_printf (buf, "<center> <font color=darkred>File not found\r\n</font></center><br>");

      evhttp_add_header (req -> output_headers, 
                   "Server", (const char *) WString_getText(server_name));
      
      evhttp_add_header (req -> output_headers, 
                   "Warc-Version", (const char *) WARC_VERSION);

      evhttp_send_reply (req, HTTP_NOTFOUND, "File not found", buf);

      return (WARC_TRUE);
      }
      
      
      /* creating an integer descriptor of the file */
   if (offset < 0)
      {
       evbuffer_add_printf (buf, "<center><font color=darkred>You gived a wrong offset\r\n</font></center><br>");

       evhttp_add_header (req -> output_headers, 
                  "Server", (const char *) WString_getText(server_name));
      
       evhttp_add_header (req -> output_headers, 
                  "Warc-Version", (const char *) WARC_VERSION);

       evhttp_send_reply (req, HTTP_NOTFOUND, "Bad offset", buf);
       w_fclose (full_file);

       return (WARC_TRUE);
       }
      
   evbuffer_expand (buf, WSERVER_MAX_BUFFER_SIZE);
      
   w_file_size (full_file, size);
   w_fseek_from_start (full_file, offset);

   size = size - (warc_u32_t) w_ftell (full_file);

      
   buffer = wmalloc (WSERVER_MAX_CHUNK_SIZE + 1);
   unless (buffer)
      {
      evbuffer_add_printf (buf, "<center><font color=darkred>Bad memory allocation\r\n</font></center><br>");
        
      evhttp_add_header (req -> output_headers, 
                 "Server", (const char *) WString_getText(server_name));
            
      evhttp_add_header (req -> output_headers, 
                 "Warc-Version", (const char *) WARC_VERSION);
            
      evhttp_send_reply (req, HTTP_NOTFOUND, "Bad memory allocation", buf);

      w_fclose (full_file);

      return (WARC_TRUE);
      }

   evhttp_add_header (req -> output_headers, 
                   "Server", (const char *) WString_getText(server_name));
     
   evhttp_add_header (req -> output_headers, 
                   "Warc-Version", (const char *) WARC_VERSION);

   /* starting the chunked transmition */
   w_numToString (size, char_size);
   evhttp_add_header (req -> output_headers, 
                      "Content-Length", (const char *) char_size);

   content_type[0] = '\0';
   if (comp_mode == WARC_FILE_COMPRESSED_GZIP)
      strcpy ((char *) content_type, "application/x-gzip");
   else if (comp_mode == WARC_FILE_UNCOMPRESSED)
      strcpy ((char *) content_type, "text/plain");
   else 
      strcpy ((char *) content_type, "");

   evhttp_add_header (req -> output_headers, 
                      "Content-Type", (const char *)content_type);

   evhttp_send_reply_start (req, HTTP_OK, "OK");

   /* sending the file chunk per chunk */
   while (size)
      {
       warc_i32_t ret = -1;
      
       /* getting a chunk from the input file */
       if (size > WSERVER_MAX_CHUNK_SIZE)
         {
           rsize = WSERVER_MAX_CHUNK_SIZE;
            size -= rsize;
         }
       else
         {
           rsize = size;
           size  = 0;
         }

       ret = w_fread (buffer, 1, rsize, full_file);
       if (w_ferror (full_file))
         break;    
         
       if ((warc_u32_t) ret < rsize && ! w_feof (full_file))
         break;

       ret = evbuffer_add (buf, buffer, ret);         
        if (ret < 0)
            break;

       /* sending thye chunk to the client */
       evhttp_send_reply_chunk (req, buf);
       evbuffer_drain (buf, ret);
     }

   wfree (buffer);

   /* ending the chunked transmition */
   evhttp_send_reply_end (req);
   w_fclose (full_file);

  return (WARC_FALSE);
}


/**
 * @param fname: the name of the concerned warc file as a WString
 * @param pref: the prefix to add to the file name to be opened as a WString
 * @param comp_mode: the warc file compression mode
 * @param offset: the offset of the wanted record
 * @param server_name: the server_name as a WString
 * @param req: the request object used in the response
 * @param buf: the buffer object used in the response
 *
 * @return False if succeeds, True otherwise
 *
 * send a response to a specified record request
 */

WPRIVATE warc_bool_t WSend_record (void * fname, void * pref, wfile_comp_t comp_mode, warc_i32_t offset, const void * server_name, 
                                          struct evhttp_request * req, struct evbuffer * buf)
{
   char         *    buffer   = NIL  ;
   void         *    wfile    = NIL  ;
   void         *    wrecord  = NIL  ;
   warc_u32_t        size     = 0    ;
   warc_u32_t        rsize    = 0    ;
   warc_u8_t         char_size[26]   ;
   warc_u8_t         content_type[25];
      

   /* 100 isn't used inside the bless below because of reading */
   wfile = bless (WFile, WString_getText (fname), 100, 
                  WARC_FILE_READER, comp_mode, WString_getText (pref), WString_getLength(pref));
   unless (wfile)
     {
      evbuffer_add_printf (buf, "<center><font color=darkred>File %s not Found\r\n</font></center><br>", WString_getText (fname));

      evhttp_add_header (req -> output_headers, 
                       "Server", (const char *) WString_getText(server_name));
      
      evhttp_add_header (req -> output_headers, 
                       "Warc-Version", (const char *) WARC_VERSION);

      evhttp_send_reply (req, HTTP_NOTFOUND, "File not found", buf);

      return (WARC_TRUE);
     }
      
   /* Seeking the requird record*/
   if (WFile_seek (wfile, offset))
     {
       evbuffer_add_printf (buf, "<center><font color=darkred>Record can not be reached\r\n</font></center><br>");

       evhttp_add_header (req -> output_headers, 
                      "Server", (const char *) WString_getText(server_name));
     
       evhttp_add_header (req -> output_headers, 
                      "Warc-Version", (const char *) WARC_VERSION);

       evhttp_send_reply (req, HTTP_NOTFOUND, "No record", buf);
       destroy (wfile);

       return (WARC_TRUE);
     }
      
   /* collecting Record's informations */
   wrecord = WFile_nextRecord (wfile);

   unless (wrecord)
     {
      evbuffer_add_printf (buf, "<center><font color=darkred>Bad offset or Bad record\r\n</font></center><br>");

      evhttp_add_header (req -> output_headers, 
                       "Server", (const char *) WString_getText(server_name));
      
      evhttp_add_header (req -> output_headers, 
                       "Warc-Version", (const char *) WARC_VERSION);

      evhttp_send_reply (req, HTTP_NOTFOUND, "Bad record", buf);
      destroy (wfile);

      return (WARC_TRUE);
     }
      
   size = WRecord_getCompressedSize (wrecord);
   destroy (wrecord);
      
   evbuffer_expand (buf, WSERVER_MAX_BUFFER_SIZE);
      
   /* coming back to the record begining */
   WFile_seek (wfile, offset);
      
   /* if failed to read, we exit the loop */
   buffer = wmalloc (WSERVER_MAX_CHUNK_SIZE + 1);
   unless (buffer)
     {
      evbuffer_add_printf (buf, "<center><font color=darkred>Bad memory allocation\r\n</font></center><br>");
        
      evhttp_add_header (req -> output_headers, 
                 "Server", (const char *) WString_getText(server_name));
            
      evhttp_add_header (req -> output_headers, 
                 "Warc-Version", (const char *) WARC_VERSION);
            
      evhttp_send_reply (req, HTTP_NOTFOUND, "Bad memory allocation", buf);
      destroy (wfile);

      return (WARC_TRUE);
      }

   evhttp_add_header (req -> output_headers, 
                      "Server", (const char *) WString_getText(server_name));
      
   evhttp_add_header (req -> output_headers, 
                      "Warc-Version", (const char *) WARC_VERSION);

   w_numToString (size, char_size);
   evhttp_add_header (req -> output_headers, 
                      "Content-Length", (const char *)char_size);

   content_type[0] = '\0';
   if (comp_mode == WARC_FILE_COMPRESSED_GZIP)
      strcpy ((char *) content_type, "application/x-gzip");
   else if (comp_mode == WARC_FILE_UNCOMPRESSED)
      strcpy ((char *) content_type, "text/plain");
   else 
      strcpy ((char *) content_type, "");

   evhttp_add_header (req -> output_headers, 
                      "Content-Type", (const char *)content_type);
      
   /* starting the chunked reply */
   evhttp_send_reply_start (req, HTTP_OK, "OK");
   /* sending the data chunk per chunk */
   while (size)
      {
       warc_i32_t ret = -1;
         
       /* getting a chunk from the input file */
       if (size > WSERVER_MAX_CHUNK_SIZE)
         {
           rsize = WSERVER_MAX_CHUNK_SIZE;
           size -= rsize;
         }
       else
         {
           rsize = size;
           size  = 0;
         }
                   
       if (WFile_fillBuffer (wfile, buffer, rsize, & ret))
         break;
          
       ret = evbuffer_add (buf, buffer, ret);         
       if (ret < 0)
         break;
          
       /* sending thye chunk to the client */
       evhttp_send_reply_chunk (req, buf);
       evbuffer_drain (buf, ret);
     }
      
   wfree (buffer);

   /* ending the chunked reply */
   evhttp_send_reply_end (req);
   destroy (wfile);
     

  return (WARC_FALSE);
}


/**
 * @param fname: the name of the warc file as a WString
 * @param pref: the prefix to add to the file name to be opened as a WString
 * @param comp_mode: the warc file compression mode
 * @param offset: the offset of the wanted record
 * @param server_name: the server_name as a WString
 * @param req: the request object used in the response
 * @param buf: the buffer object used in the response
 * 
 * @return False if succeeds, True otherwise
 *
 * Send informations about a warc file from a specified offset
 */

WPRIVATE warc_bool_t WSend_distantDumpResponse (void * fname, void * pref, wfile_comp_t comp_mode, warc_i32_t offset, const void * server_name, 
                                                            struct evhttp_request * req, struct evbuffer * buf)
{
    void        *    wfile     = NIL ;
    void        *    wrecord   = NIL ;
    warc_u32_t       cpt       = 0   ;

   /* 100 isn't used inside the bless below because of reading */
   wfile = bless (WFile, WString_getText (fname), 100, 
                  WARC_FILE_READER, comp_mode, WString_getText (pref), WString_getLength(pref));
   unless (wfile)
     {
      evbuffer_add_printf (buf, "<center><font color=darkred>File %s not Found\r\n</font></center><br>", WString_getText (fname));

      evhttp_add_header (req -> output_headers, 
                       "Server", (const char *) WString_getText(server_name));
      
      evhttp_add_header (req -> output_headers, 
                       "Warc-Version", (const char *) WARC_VERSION);

      evhttp_send_reply (req, HTTP_NOTFOUND, "File not found", buf);

      return (WARC_TRUE);
     }
      
   /* Seeking the requird record*/
   if (WFile_seek (wfile, offset))
     {
       evbuffer_add_printf (buf, "<center><font color=darkred>Offset can not be reached\r\n</font></center><br>");

       evhttp_add_header (req -> output_headers, 
                      "Server", (const char *) WString_getText(server_name));
     
       evhttp_add_header (req -> output_headers, 
                      "Warc-Version", (const char *) WARC_VERSION);

       evhttp_send_reply (req, HTTP_NOTFOUND, "Bad offset", buf);
       destroy (wfile);

       return (WARC_TRUE);
     }
      

  while (WFile_hasMoreRecords (wfile) )
    {
      const void * al  = NIL; /* ANVL list object */

      unless ( (wrecord = WFile_nextRecord (wfile) ) )
          {
          evbuffer_add_printf (buf, "<center><font color=darkred>Bad record found!\r\n</font></center><br>");

          evhttp_add_header (req -> output_headers, 
                         "Server", (const char *) WString_getText(server_name));
     
          evhttp_add_header (req -> output_headers, 
                         "Warc-Version", (const char *) WARC_VERSION);

          evhttp_send_reply (req, HTTP_NOTFOUND, "Bad Record", buf);

          break;
         }

      cpt ++;
      /* dump WRecord */
      evbuffer_add_printf (buf, "<font color=darkblue size = 5> <b> Warc Record: %d</b> </font>", cpt);
      evbuffer_add_printf (buf, "<blockquote> <table BORDER = 2 bgcolor=lightgray>");
      evbuffer_add_printf (buf, "<tr><td><font color=darkgreen size = 3> Offset </font></td><td>%-10llu</td></tr>",
                              (unsigned long long) WRecord_getOffset (wrecord) );

      evbuffer_add_printf (buf, "<tr><td><font color=darkgreen size = 3> Compressed size </font></td><td>%-10llu</td></tr>",
                              (unsigned long long) WRecord_getCompressedSize (wrecord) );

      evbuffer_add_printf (buf, "<tr><td><font color=darkgreen size = 3> Warc ID </font></td><td>%-10s</td></tr> ",
                              (const char *) WRecord_getWarcId      (wrecord) );

      evbuffer_add_printf (buf, "<tr><td><font color=darkgreen size = 3> Data Length </font></td><td>%-10u</td></tr>",
                              WRecord_getDataLength  (wrecord) );

      evbuffer_add_printf (buf, "<tr><td><font color=darkgreen size = 3> Record Type </font></td><td>%-15u</td></tr>",
                              WRecord_getRecordType  (wrecord) );

      evbuffer_add_printf (buf, "<tr><td><font color=darkgreen size = 3> Content Type </font></td><td>%-14s</td></tr>",
                              (const char *) WRecord_getCreationDate (wrecord) );

      evbuffer_add_printf (buf, "<tr><td><font color=darkgreen size = 3> Compressed size </font></td><td>%-20s</td></tr>",
                              (const char *) WRecord_getContentType (wrecord) );

      evbuffer_add_printf (buf, "<tr><td><font color=darkgreen size = 3> Record ID </font></td><td>%-56s</td></tr>",
                              (const char *) WRecord_getRecordId    (wrecord) );

      evbuffer_add_printf (buf, "<tr><td><font color=darkgreen size = 3> Subject Uri </font></td><td>%-100s</td></tr>",
                              (const char *) WRecord_getSubjectUri  (wrecord) );

      evbuffer_add_printf (buf, "</table></blockquote><br>");

      /* dump ANVLs */

      if ((al = WRecord_getAnvl (wrecord) ))
        {
          warc_u32_t  i = 0;
          warc_u32_t  j = WList_size (al); /* how many ANVL are there? */
        
         if (j)
           {
           evbuffer_add_printf (buf, "<font color=darkred size = 4> <b> More informations </b> </font>");
           evbuffer_add_printf (buf, "<blockquote> <table BORDER = 2 bgcolor=lightgray>");
       
           while ( i < j )
               {
                 const void  * a = WList_get (al, i); /* ANVL record */
   
                 /* we assume here that the ANVL value was in ASCII. */
                 /* use your own encoding to print it otherwise. */
                 evbuffer_add_printf (buf , "<tr><td><font color=darkgreen size = 3> %s </font></td><td>%s</td></tr>",
                                          (const char *) WAnvl_getKey (a), (const char *) WAnvl_getValue (a) );

                 ++ i;
               }
           evbuffer_add_printf (buf, "</table></blockquote><br><br>");
           }

        }

      destroy (wrecord);
    }

   evhttp_add_header (req -> output_headers, 
                      "Server", (const char *) WString_getText(server_name));
      
   evhttp_add_header (req -> output_headers, 
                      "Warc-Version", (const char *) WARC_VERSION);

   evhttp_add_header (req -> output_headers, 
                      "Content-Type", "text/html");

   evhttp_send_reply (req, HTTP_OK, "OK", buf);

   destroy (wfile);

  return (WARC_FALSE);
}


/**
 * @param fname: the name of the concerned warc file as a WString
 * @param pref: the prefix to add to the file name to be opened as a WString
 * @param comp_mode: the warc file compression mode
 * @param offset: the offset of the wanted record
 * @param what: on what the filter will be done
 * @param value: the value of the filter as a WString
 * @param server_name: the server_name as a WString
 * @param req: the request object used in the response
 * @param buf: the buffer object used in the response
 *
 * @return False if succeeds, True otherwise
 *
 * send a response to a specified record request
 */

WPRIVATE warc_bool_t WSend_distantFilterResponse (void * fname, void * pref, wfile_comp_t comp_mode, warc_i32_t offset, warc_filters_t what, void * value,
                                                   const void * server_name, struct evhttp_request * req, struct evbuffer * buf)
{
   char         *    buffer   = NIL  ;
   void         *    wfile    = NIL  ;
   void         *    wrecord  = NIL  ;
   warc_u32_t        size     = 0    ;
   warc_u32_t        rsize    = 0    ;
   warc_u8_t         content_type[25];
   warc_bool_t       found    = WARC_TRUE ;
   const warc_u8_t   * string = NIL ;
   warc_rec_t        rtype    = WARC_UNKNOWN_RECORD;
   warc_i32_t        soffset  = offset;
   

   /* 100 isn't used inside the bless below because of reading */

   wfile = bless (WFile, WString_getText (fname), 100, 
                  WARC_FILE_READER, comp_mode, WString_getText (pref), WString_getLength(pref));
   unless (wfile)
     {
      evbuffer_add_printf (buf, "<center><font color=darkred>File %s not Found\r\n</font></center><br>", WString_getText (fname));

      evhttp_add_header (req -> output_headers, 
                       "Server", (const char *) WString_getText(server_name));
      
      evhttp_add_header (req -> output_headers, 
                       "Warc-Version", (const char *) WARC_VERSION);

      evhttp_send_reply (req, HTTP_NOTFOUND, "File not found", buf);

      return (WARC_TRUE);
     }
      
   buffer = wmalloc (WSERVER_MAX_CHUNK_SIZE + 1);
   unless (buffer)
     {
      evbuffer_add_printf (buf, "<center><font color=darkred>Bad memory allocation\r\n</font></center><br>");
        
      evhttp_add_header (req -> output_headers, 
                 "Server", (const char *) WString_getText(server_name));
            
      evhttp_add_header (req -> output_headers, 
                 "Warc-Version", (const char *) WARC_VERSION);
            
      evhttp_send_reply (req, HTTP_NOTFOUND, "Bad memory allocation", buf);
      destroy (wfile);

      return (WARC_TRUE);
      }

   evhttp_add_header (req -> output_headers, 
                      "Server", (const char *) WString_getText(server_name));
      
   evhttp_add_header (req -> output_headers, 
                      "Warc-Version", (const char *) WARC_VERSION);


  /* Seeking the required offset*/
  if (WFile_seek (wfile, soffset))
    {
      evbuffer_add_printf (buf, "<center><font color=darkred>Record can not be reached\r\n</font></center><br>");

      evhttp_add_header (req -> output_headers, 
                     "Server", (const char *) WString_getText(server_name));
     
      evhttp_add_header (req -> output_headers, 
                       "Warc-Version", (const char *) WARC_VERSION);

      evhttp_send_reply (req, HTTP_NOTFOUND, "No record", buf);
      wfree (buffer);
      destroy (wfile);

      return (WARC_TRUE);
    }


   content_type[0] = '\0';
   if (comp_mode == WARC_FILE_COMPRESSED_GZIP)
      strcpy ((char *) content_type, "application/x-gzip");
   else if (comp_mode == WARC_FILE_UNCOMPRESSED)
      strcpy ((char *) content_type, "text/plain");
   else 
      strcpy ((char *) content_type, "");

   evhttp_add_header (req -> output_headers, 
                      "Content-Type", (const char *)content_type);
      
   /* starting the chunked reply */
   evhttp_send_reply_start (req, HTTP_OK, "OK");

   while (WFile_hasMoreRecords (wfile))
     {

      /* collecting Record's informations */
      wrecord = WFile_nextRecord (wfile);

      unless (wrecord)
        {
         evbuffer_add_printf (buf, "<center><font color=darkred>Bad offset or Bad record\r\n</font></center><br>");

         evhttp_send_reply_end (req);
         wfree (buffer);
         destroy (wfile);

         return (WARC_TRUE);
        }
      
      size = WRecord_getCompressedSize (wrecord);

      switch (what)
         {
         case WARC_URI:
             string = WRecord_getSubjectUri (wrecord);
             found  = match (makeS (string), WString_getText (value), WString_getLength (value));

         break;

         case WARC_MIME:
             string = WRecord_getContentType (wrecord);
             found  = match (makeS (string), WString_getText (value), WString_getLength (value));

             break;

         case WARC_RTYPE:
             rtype = WRecord_getRecordType (wrecord);

             found = WARC_TRUE;
             switch (rtype)
               {
               case WARC_RESOURCE_RECORD:
                    unless (w_strcmp (WString_getText (value), (const warc_u8_t *) "resource"))
                       found = WARC_FALSE;
                    break;

               case WARC_CONTINUATION_RECORD:
                    unless (w_strcmp (WString_getText (value), (const warc_u8_t *) "continuation"))
                      found = WARC_FALSE;
                    break;

               case WARC_CONVERSION_RECORD:
                    unless (w_strcmp (WString_getText (value), (const warc_u8_t *) "conversion"))
                      found = WARC_FALSE;
                    break;

               case WARC_REVISIT_RECORD:
                    unless (w_strcmp (WString_getText (value), (const warc_u8_t *) "revisit"))
                      found = WARC_FALSE;
                    break;

               case WARC_METADATA_RECORD:
                    unless (w_strcmp (WString_getText (value), (const warc_u8_t *) "metadata"))
                      found = WARC_FALSE;
                    break;

               case WARC_REQUEST_RECORD:
                    unless (w_strcmp (WString_getText (value), (const warc_u8_t *) "request"))
                      found = WARC_FALSE;
                    break;

               case WARC_RESPONSE_RECORD:
                    unless (w_strcmp (WString_getText (value), (const warc_u8_t *) "response"))
                      found = WARC_FALSE;
                    break;

               case WARC_INFO_RECORD:
                    unless (w_strcmp (WString_getText (value), (const warc_u8_t *) "warcinfo"))
                      found = WARC_FALSE;
                    break;

               case WARC_UNKNOWN_RECORD:
                    break;
               }
               break;

         default: break;
         }

      destroy (wrecord);
      WFile_seek (wfile, soffset);
      soffset = soffset + size;

      unless (found)
         {
         buffer[0] = '\0';
         evbuffer_expand (buf, WSERVER_MAX_BUFFER_SIZE);
      
         /* coming back to the record begining */

         /* sending the data chunk per chunk */
         while (size)
            {
             warc_i32_t ret = -1;
         
             /* getting a chunk from the input file */
             if (size > WSERVER_MAX_CHUNK_SIZE)
               {
                 rsize = WSERVER_MAX_CHUNK_SIZE;
                 size -= rsize;
               }
             else
               {
                rsize = size;
                size  = 0;
               }
                   
             if (WFile_fillBuffer (wfile, buffer, rsize, & ret))
               break;
          
             ret = evbuffer_add (buf, buffer, ret);         
             if (ret < 0)
               break;
          
             /* sending thye chunk to the client */
             evhttp_send_reply_chunk (req, buf);
             evbuffer_drain (buf, ret);
            }
      
        }

     /* Seeking the required offset*/
     if (WFile_seek (wfile, soffset))
       {
         evbuffer_add_printf (buf, "<center><font color=darkred>Record can not be reached\r\n</font></center><br>");

         evhttp_add_header (req -> output_headers, 
                        "Server", (const char *) WString_getText(server_name));
     
         evhttp_add_header (req -> output_headers, 
                        "Warc-Version", (const char *) WARC_VERSION);

         evhttp_send_reply_end (req);
         wfree (buffer);
         destroy (wfile);

         return (WARC_TRUE);
       }

     }

   wfree (buffer);

   /* ending the chunked reply */
   evhttp_send_reply_end (req);
   destroy (wfile);
     

  return (WARC_FALSE);
}


/* callback function to manage the Record accessing request of the client */
WPRIVATE void WAccess_callback (struct evhttp_request * req, void * _arg)
{
  struct Callbacks_arg * arg = _arg;

  const void       * server_name = NIL;
  void       * pref        = NIL;
  void             * fname       = NIL;
  void             * cvalue      = NIL;
  const  warc_u8_t * uri         = NIL;
  struct evbuffer  * buf         = NIL;
  warc_i32_t         offset      = -1;
  wfile_comp_t       comp_mode;
  warc_request_t     req_kind;
  warc_filters_t     what;


  server_name = arg -> server_name;
  pref        = arg -> server_prefix;
  uri = (const warc_u8_t *) evhttp_request_uri (req);

  buf = evbuffer_new ();
  unless (buf)
  {
    WarcDebugMsg ("Can't create a buffer for transmission\n");

    evhttp_add_header (req -> output_headers, 
                       "Server", (const char *) WString_getText(server_name));
    
    evhttp_add_header (req -> output_headers, 
                       "Warc-Version", (const char *) WARC_VERSION);
    
    evhttp_send_reply (req, HTTP_NOCONTENT, "No buffer", NIL);
    return;
  }
  
  if (arg -> shoot_down)
    {
      evbuffer_add_printf (buf, "<center><font color=darkred>The server is shooting down\r\n</font></center><br>");
      
      evhttp_add_header (req -> output_headers, 
                         "Server", (const char *) WString_getText(server_name));
      
      evhttp_add_header (req -> output_headers, 
                         "Warc-Version", (const char *) WARC_VERSION);
      
      evhttp_send_reply (req, HTTP_NOTFOUND, "Server is shooting down", buf);
      evbuffer_free (buf);
      return;
    }
 
  fname = bless (WString, "", 0);
  unless (fname)
  {
    WarcDebugMsg ("Can't create a string object to hold the warc file name\n");
    evbuffer_add_printf (buf, "<center><font color=darkblue size=2>Internal error, can't stisfy the request. Please retry\r\n</font></center><br>");

      evhttp_add_header (req -> output_headers, 
                         "Server", (const char *) WString_getText(server_name));
      
      evhttp_add_header (req -> output_headers, 
                         "Warc-Version", (const char *) WARC_VERSION);

    evhttp_send_reply (req, HTTP_NOCONTENT, "No String", buf);
    evbuffer_free (buf);
    return ;
  }
  
  cvalue = bless (WString, "", 0);
  unless (cvalue)
  {
    WarcDebugMsg ("Can't create a string object to hold the filter criteria value\n");
    evbuffer_add_printf (buf, "<center><font color=darkblue size=2>Internal error, can't stisfy the request. Please retry\r\n</font></center><br>");

      evhttp_add_header (req -> output_headers, 
                         "Server", (const char *) WString_getText(server_name));
      
      evhttp_add_header (req -> output_headers, 
                         "Warc-Version", (const char *) WARC_VERSION);

    evhttp_send_reply (req, HTTP_NOCONTENT, "No String", buf);
    destroy (fname);
    evbuffer_free (buf);
    return ;
  }

  if (WServer_parseRequest (uri, fname, & comp_mode, 
                            & offset, &what, cvalue, & req_kind))
    {
      evbuffer_add_printf (buf, "<center> <b> <font color=darkblue size=2> Bad request format\r\n </font> </b> </center><br>");
      evbuffer_add_printf (buf, "The correct format is:<br>/warc/version/record/compression_mode/record_offset/warc_file_path\r\n<br>");
      evbuffer_add_printf (buf, "Or:<br>/warc/version/file/compression_mode/offset/warc_file_path\r\n<br>");
      evbuffer_add_printf (buf, "Or:<br>/warc/version/list/compression_mode/offset/warc_file_path\r\n<br>");
      evbuffer_add_printf (buf, "Or:<br>/warc/version/filter/compression_mode/offset/what_to_filter_on/filter_value/warc_file_path\r\n<br>");

      evhttp_add_header (req -> output_headers, 
                         "Server", (const char *) WString_getText(server_name));
      
      evhttp_add_header (req -> output_headers, 
                         "Warc-Version", (const char *) WARC_VERSION);

      evhttp_send_reply (req, HTTP_BADREQUEST, "Bad request", buf);
      evbuffer_free (buf);
      destroy (fname);
      destroy (cvalue);
      return;
    }
  
  /* testing what is requested */
  switch (req_kind)
    {
     case  WARC_FULL_FILE_REQUEST :

        /* here a full warc file is requested */
        destroy (cvalue);
        WSend_fullResponse (fname, pref, comp_mode, offset, server_name, req, buf);
        destroy (fname);
        evbuffer_free (buf);

        break;
 
     case WARC_ONE_RECORD_REQUEST :

        /* here, only one record is requested */
        destroy (cvalue);
        WSend_record (fname, pref, comp_mode, offset, server_name, req, buf);

        destroy (fname);
        evbuffer_free (buf);

        break;
    case WARC_DISTANT_DUMP_REQUEST :

        /* here we list informations about the warc file records */
        destroy (cvalue);
        WSend_distantDumpResponse (fname, pref, comp_mode, offset, server_name, req, buf);

        destroy (fname);
        evbuffer_free (buf);

        break;

    case WARC_DISTANT_FILTER_REQUEST :

       /* here, we send records filtred in a particular criteria */
       WSend_distantFilterResponse (fname, pref, comp_mode, offset, what, cvalue, server_name, req, buf);

       destroy (cvalue);
       destroy (fname);
       evbuffer_free (buf);

    default: break;
    }
  
}


WPRIVATE void WStop_callback (struct evhttp_request * req, void * _arg)
{
  struct Callbacks_arg * arg         = _arg;
  warc_bool_t          * shoot_down  = & (arg -> shoot_down);
  const void           * server_name = arg -> server_name;
  struct event_base    * base        = arg -> base;
  struct evbuffer      * buf         = evbuffer_new ();
  
  unless (buf)
  {
    WarcDebugMsg ("Can't create buffer during the stopping operation if the server\n");

    evhttp_add_header (req -> output_headers, 
                       "Server", (const char *) WString_getText(server_name));
    
    evhttp_add_header (req -> output_headers, 
                       "Warc-Version", (const char *) WARC_VERSION);
    
    evhttp_send_reply (req, HTTP_NOTMODIFIED, "Not stopped", NIL);
  }
  else
    {
#define WSERVER_LOCAL_HOST "127.0.0.1"
      if (w_strcmp ((const warc_u8_t *) req -> remote_host, 
                    (const warc_u8_t *) WSERVER_LOCAL_HOST ))
        {
          evbuffer_add_printf (buf, 
       "<center><font color=darkred>Unable to stop the server. You need to be logged on the same host\r\n</font></center><br>");
          
          evhttp_add_header (req -> output_headers, 
                "Server", (const char *) WString_getText(server_name));
          
          evhttp_add_header (req -> output_headers, 
                "Warc-Version", (const char *) WARC_VERSION);

          evhttp_send_reply (req, HTTP_BADREQUEST, "Can't do this", buf);
        }
      else
        {
          * shoot_down = WARC_TRUE;
          
          event_base_loopexit (base, NIL);
          evbuffer_add_printf (buf, "<b> <center> <font color=darkred size =2> Stopping the WARC server\r\n </font></center> </b><br>");
          
          evhttp_add_header (req -> output_headers, 
                    "Server", (const char *) WString_getText(server_name));
          
          evhttp_add_header (req -> output_headers, 
                    "Warc-Version", (const char *) WARC_VERSION);

          evhttp_add_header (req -> output_headers, 
                    "Content-Type", "text/html");

          evhttp_send_reply (req, HTTP_OK, "Stopping the server", buf);
          
        }
    evbuffer_free (buf);
    }
}



/**
 * WARC WServer class internal
 */

#define SIGN 18

struct WServer
  {
    const void * class;

    /*@{*/
    void * listening_uri; /**< The Uri wher the server is listening for requests */
    void * prefix; /**< directory prefix to append to any request */
    warc_u16_t listening_port; /**< The listening port */
    void * server_name; /**< Server name in the HTTP header */
    struct event_base * base_event; /**< The base event associated with the server */
    struct evhttp * server_instance; /**< the evhttp object instance of the server */
    void (* access_handler) (struct evhttp_request *, void *); /**< the handler of the REST File access requests */
    void (* stop_handler)   (struct evhttp_request *, void *); /**< the handler of the stop request which will stop the server */
    /*@}*/
  };


#define   URI           (self -> listening_uri)
#define   PREFIX        (self -> prefix)
#define   PORT          (self -> listening_port)
#define   BASE          (self -> base_event)
#define   SERV          (self -> server_instance)
#define   ACCESSH       (self -> access_handler)
#define   STOPCB        (self -> stop_handler)
#define   SERVER_NAME   (self -> server_name)


/**
 * @param _self: WServer class object
 * 
 * @return False if succeeds, True otherewise
 *
 *  Wserver initialisation function
 */

WPRIVATE warc_bool_t WServer_init (void * _self)
{
  struct WServer * self = _self;

  /* Precondition */
  CASSERT (self);

  BASE = event_init();
  unless (BASE)
    return (WARC_TRUE);

  SERV = evhttp_new (BASE);
  unless (SERV)
    return (WARC_TRUE);

  evhttp_bind_socket (SERV, (const char *) WString_getText (URI), PORT);


  ACCESSH = WAccess_callback;
  STOPCB  = WStop_callback;

  return (WARC_FALSE);
}


/**
 * @param _self: WServer object instance
 *
 * @return False if succeeds, True otherwise
 *
 * WServer starting function
 */

WPUBLIC warc_bool_t WServer_start (void * _self)
{
  struct WServer * self = _self;
  
  struct Callbacks_arg arg;
  
  /* Precondition */
  CASSERT (self);
  
  arg . base          = BASE;
  arg . shoot_down    = WARC_FALSE;
  arg . server_name   = SERVER_NAME;
  arg . server_prefix = PREFIX;

  evhttp_set_cb    (SERV, "/" WARC_VERSION "/stop", STOPCB, & arg);
  evhttp_set_gencb (SERV, ACCESSH, & arg);

  event_base_dispatch (BASE);

  return (WARC_FALSE);
}


/**
 * @param _self: WServer class object
 * 
 * @return False if succeeds, True otherewise
 *
 *  Wserver ending function
 */

WPUBLIC warc_bool_t WServer_stop (void * _self)
{
  struct WServer * self = _self;
  warc_bool_t      out  = WARC_FALSE;

  /* Precondition */
  CASSERT (self);

  unless (SERV)
    out = WARC_TRUE;
  else
    evhttp_free (SERV), SERV = NIL;

  unless (BASE)
    out = WARC_TRUE;
  else
    event_base_free(BASE), BASE = NIL;
  
  return (out);
}



/**
 * WServer_constructor - create a new WServer object instance
 *
 * @param _self: WServer class object
 * @param app constructor list parameters
 *
 * @return A valid WServer object or NIL
 *
 * @brief WARC List constructor
 */

WPRIVATE void * WServer_constructor (void * _self, va_list * app)
{

  struct WServer * const self = _self;

  const warc_u8_t * uri            = va_arg (*app, const warc_u8_t *);
  warc_u32_t        uri_len        = va_arg (*app, warc_u32_t);
  warc_u16_t        port           = va_arg (*app, warc_u32_t);
  const char *      servername     = va_arg (*app, const char *);
  warc_u32_t        servername_len = va_arg (*app, warc_u32_t);
  const char *      prefix         = va_arg (*app, const char *);
  warc_u32_t        prefix_len     = va_arg (*app, warc_u32_t);

  URI = bless (WString, uri, uri_len);
  unless (URI)
	{
      destroy (self);
      return (NIL);
    }
  
  PREFIX = bless (WString, prefix, prefix_len);
  unless (PREFIX)
	{
      destroy (self);
      return (NIL);
    }

  SERVER_NAME = bless (WString, servername, servername_len);
  unless (SERVER_NAME)
	{
      destroy (self);
      return (NIL);
    }

  PORT = port;
  unless (PORT)
    {
      destroy (self);
      return (NIL);
    }
  
  SERV    = NIL;
  BASE    = NIL;
  ACCESSH = NIL;
  STOPCB  = NIL;
  
  /* intialize the callbacks */
  if (WServer_init (self))
    {
      destroy (self);
      return (NIL);
    }
    
  return (self);
}


/**
 * @param _self WServer object instance
 *
 * @brief WARC List destructor
 */

WPRIVATE void * WServer_destructor (void * _self)
{

  struct WServer  * const self = _self;

  /* preconditions */
  CASSERT (self);

  if (URI)
    destroy (URI), URI = NIL;

  if (PREFIX)
    destroy (PREFIX), PREFIX = NIL;

  if (SERVER_NAME)
    destroy (SERVER_NAME), SERVER_NAME = NIL;

  if (PORT)
    PORT = 0;

  if (BASE)
    BASE = NIL;

  if (SERV)
    SERV = NIL;

  if (ACCESSH)
    ACCESSH = NIL;

  if (STOPCB)
    STOPCB = NIL;

  return (self);
}


/**
 * WARC WServer class
 */

static const struct Class _WServer =
  {
    sizeof (struct WServer),
    SIGN,
    WServer_constructor, WServer_destructor
  };

const void * WServer = & _WServer;
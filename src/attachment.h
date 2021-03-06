/* attachment.h - Wrapper class for attachments
 * Copyright (C) 2005, 2007 g10 Code GmbH
 * Copyright (C) 2015, 2016 by Bundesamt für Sicherheit in der Informationstechnik
 * Software engineering by Intevation GmbH
 *
 * This file is part of GpgOL.
 *
 * GpgOL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * GpgOL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef ATTACHMENT_H
#define ATTACHMENT_H

#include <string>

#include <gpgme++/data.h>

/** Helper class for attachment actions. */
class Attachment
{
public:
  /** Creates and opens a new in memory attachment. */
  Attachment();
  ~Attachment();

  /** Set the display name */
  void set_display_name(const char *name);
  std::string get_display_name() const;

  void set_attach_type(attachtype_t type);

  /* Content id */
  void set_content_id (const char *cid);
  std::string get_content_id() const;

  /* Content type */
  void set_content_type (const char *type);
  std::string get_content_type () const;

  /* get the underlying data structure */
  GpgME::Data& get_data();

private:
  GpgME::Data m_data;
  std::string m_utf8DisplayName;
  attachtype_t m_type;
  std::string m_cid;
  std::string m_ctype;
};

#endif // ATTACHMENT_H

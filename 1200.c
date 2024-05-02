goa_ews_client_autodiscover (GoaEwsClient        *client,
                             const gchar         *email,
                             const gchar         *password,
                             const gchar         *username,
                             const gchar         *server,
                             gboolean             accept_ssl_errors,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  AutodiscoverData *data;
  AutodiscoverAuthData *auth;
  gchar *url1;
  gchar *url2;
  xmlDoc *doc;
  xmlOutputBuffer *buf;

  g_return_if_fail (GOA_IS_EWS_CLIENT (client));
  g_return_if_fail (email != NULL || email[0] != '\0');
  g_return_if_fail (password != NULL || password[0] != '\0');
  g_return_if_fail (username != NULL || username[0] != '\0');
  g_return_if_fail (server != NULL || server[0] != '\0');
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  doc = ews_client_create_autodiscover_xml (email);
  buf = xmlAllocOutputBuffer (NULL);
  xmlNodeDumpOutput (buf, doc, xmlDocGetRootElement (doc), 0, 1, NULL);
  xmlOutputBufferFlush (buf);

  url1 = g_strdup_printf ("https://%s/autodiscover/autodiscover.xml", server);
  url2 = g_strdup_printf ("https://autodiscover.%s/autodiscover/autodiscover.xml", server);

  /* http://msdn.microsoft.com/en-us/library/ee332364.aspx says we are
   * supposed to try $domain and then autodiscover.$domain. But some
   * people have broken firewalls on the former which drop packets
   * instead of rejecting connections, and make the request take ages
   * to time out. So run both queries in parallel and let the fastest
   * (successful) one win.
   */
  data = g_slice_new0 (AutodiscoverData);
  data->buf = buf;
  data->res = g_simple_async_result_new (G_OBJECT (client), callback, user_data, goa_ews_client_autodiscover);
  data->msgs[0] = ews_client_create_msg_for_url (url1, buf);
  data->msgs[1] = ews_client_create_msg_for_url (url2, buf);
  data->session = soup_session_async_new_with_options (SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
                                                       SOUP_SESSION_SSL_STRICT, FALSE,
                                                       SOUP_SESSION_USE_NTLM, TRUE,
                                                       SOUP_SESSION_USE_THREAD_CONTEXT, TRUE,
                                                       NULL);
  data->accept_ssl_errors = accept_ssl_errors;

  if (cancellable != NULL)
    {
      data->cancellable = g_object_ref (cancellable);
      data->cancellable_id = g_cancellable_connect (data->cancellable,
                                                    G_CALLBACK (ews_client_autodiscover_cancelled_cb),
                                                    data,
                                                    NULL);
      g_simple_async_result_set_check_cancellable (data->res, data->cancellable);
    }

  auth = g_slice_new0 (AutodiscoverAuthData);
  auth->username = g_strdup (username);
  auth->password = g_strdup (password);
  g_signal_connect_data (data->session,
                         "authenticate",
                         G_CALLBACK (ews_client_authenticate),
                         auth,
                         ews_client_autodiscover_auth_data_free,
                         0);

  soup_session_queue_message (data->session, data->msgs[0], ews_client_autodiscover_response_cb, data);
  soup_session_queue_message (data->session, data->msgs[1], ews_client_autodiscover_response_cb, data);

  g_free (url2);
  g_free (url1);
  xmlFreeDoc (doc);
}

idf_component_register(SRCS
                            "main.c"
                            "webserver.c"

                        INCLUDE_DIRS
                            "."

                        EMBED_TXTFILES
                            "pages/index.html"
                            "pages/dashboard.html"
                            "pages/changePassword.html"
)

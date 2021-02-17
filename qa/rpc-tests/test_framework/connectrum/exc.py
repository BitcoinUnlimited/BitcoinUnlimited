#
# Exceptions
#


class ElectrumErrorResponse(RuntimeError):
    response = None
    request = None

    def __init__(self, response, request):
        self.response = response
        self.request = request
        super().__init__(response, request)

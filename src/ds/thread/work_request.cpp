#include "ds/thread/work_request.h"

namespace ds {

/**
 * \class ds::WorkRequest
 */
WorkRequest::WorkRequest(const void* clientId)
	: mClientId(clientId)
{
}

WorkRequest::~WorkRequest()
{
}

} // namespace ds

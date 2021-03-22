/*
Copyright 2012 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
 */

int triad(int len, int inner, double * a, double * b, int *index)
{
	int i,j,k, count, bytes=24;
	k = 0;
	count = 0;
	for(i=0; i< len; i++) 
		{
		k = index[i];
		for(j=0; j< inner; j++)
			{
			if((count & 1) == 0)
				{
				a[k] = b[k];
				}
			else
				{
				a[k] = sqrt(b[k]);
				}
			k++;
			count++;
			}
		}
	return bytes;
}

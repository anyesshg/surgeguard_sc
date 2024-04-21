import string
import random
import os

max_user_index = 962 

def main():
  for usr_idx in range(0,max_user_index):
    user_id = str(usr_idx)
    username = "username_" + user_id
    for post_idx in range(40):
      text = ''.join(random.choice(string.ascii_uppercase) for _ in range(256))
      num_user_mentions = random.randint(0, 5)
      num_urls = random.randint(0, 5)
      num_media = random.randint(0, 4)
      media_ids = ""
      media_types = ""
  
      for i in range(num_user_mentions):
        user_mention_id = random.randint(0, max_user_index - 1)
        if(usr_idx != user_mention_id):
          break
        text = text+" @username_"+str(user_mention_id)
  
      for i in range(num_urls):
        text = text+" http://"+''.join(random.choice(string.ascii_uppercase) for _ in range(64))
      
      if(num_media > 0):
        media_id = ''.join(random.choice(string.digits) for _ in range(18))
        media_ids = "media_ids[]="+media_id
        media_types = "media_types[]=\"png\""

        for i in range(2,num_media):
          media_id = ''.join(random.choice(string.digits) for _ in range(18))
          media_ids = "&media_ids[]="+media_id
          media_types = "&media_types[]=\"png\""
   
  
      command = "curl"
      headers = "Content-Type: application/x-www-form-urlencoded"
      body = "username="+ username+ "&user_id="+ user_id+ "&text="+ text
      if (num_media > 0):
        body = body + media_ids + "&" + media_types
      body = body+"&post_type=0"

      command = command + " --header \"" + headers
      command = command + "\" -d \"" + body 
      command = command + "\" http://localhost:8080/wrk2-api/post/compose"
      #print(command)
      os.system(command) 
      print(f'Posted {str(post_idx)} of user {user_id}') 

if __name__ == "__main__":
    main()
